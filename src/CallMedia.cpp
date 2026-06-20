// Bazarish project (c) 2026
#include "CallMedia.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Sam.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace bazarish {

namespace {

// Datagram wire format: a 12-byte AES-GCM nonce (cleartext) followed by the
// sealed payload (ciphertext || 16-byte tag). The nonce is
// role(1) || track(1) || 0 0 || sequence(8, big-endian).
constexpr std::size_t kNonceSize = kAeadNonceBytes;  // 12

// Track byte (nonce[1]): which media stream a datagram carries.
constexpr std::uint8_t kTrackAudio = 0;
constexpr std::uint8_t kTrackVideo = 1;
constexpr std::uint8_t kTrackControl = 2;

// Control opcodes (first byte of a control payload).
constexpr std::uint8_t kCtrlRequestKeyframe = 1;

// Per-fragment payload budget for the video track: small enough that one lost
// datagram costs little and stays well under the I2P datagram limit, leaving
// room for the nonce, tag and fragment header.
constexpr std::size_t kMaxFragmentPayload = 1024;

// Video fragment header inside the sealed payload:
// frameId(4, big-endian) || fragmentIndex(1) || fragmentCount(1).
constexpr std::size_t kVideoHeaderBytes = 6;

// A frame fragmented into more pieces than this cannot be signalled in the
// 1-byte fragment count; at the call bitrate a frame never approaches this.
constexpr std::size_t kMaxFragments = 255;

// After requesting a keyframe, wait this many inbound video fragments before
// requesting again, so a burst of loss does not flood the peer with requests.
constexpr int kKeyframeRequestCooldown = 30;

// A jump larger than this many lost frames is treated as a fresh start rather
// than synthesising a long burst of concealment audio.
constexpr std::uint64_t kMaxPlcGap = 5;

// How long the receive loop waits per poll before re-checking the stop flag.
constexpr int kReceivePollMs = 200;

Bytes makeNonce(const std::uint8_t role, const std::uint8_t track, const std::uint64_t sequence)
{
    Bytes nonce(kNonceSize, 0);
    nonce[0] = role;
    nonce[1] = track;
    for (int i = 0; i < 8; ++i) {
        nonce[kNonceSize - 1 - i] = static_cast<unsigned char>((sequence >> (8 * i)) & 0xff);
    }
    return nonce;
}

std::uint64_t sequenceFromNonce(const Bytes& nonce)
{
    std::uint64_t sequence = 0;
    for (int i = 0; i < 8; ++i) {
        sequence = (sequence << 8) | nonce[kNonceSize - 8 + i];
    }
    return sequence;
}

}  // namespace

SamCallTransport::SamCallTransport(SamDatagramSession& session, std::string peerDestination)
    : session_(session)
    , peerDestination_(std::move(peerDestination))
{
}

void SamCallTransport::sendDatagram(const void* data, const std::size_t size)
{
    session_.send(peerDestination_, data, size);
}

std::vector<std::uint8_t> SamCallTransport::receiveDatagram(const int timeoutMs)
{
    return session_.receive(timeoutMs);
}

CallMedia::CallMedia(CallTransport& transport, std::unique_ptr<AudioSource> audioSource,
    std::unique_ptr<AudioSink> audioSink, std::unique_ptr<VideoSource> videoSource,
    std::unique_ptr<VideoSink> videoSink, const Bytes& mediaKey, const CallRole role)
    : transport_(transport)
    , audioSource_(std::move(audioSource))
    , audioSink_(std::move(audioSink))
    , videoSource_(std::move(videoSource))
    , videoSink_(std::move(videoSink))
    , mediaKey_(mediaKey)
    , sendRole_(role == CallRole::eCaller ? 1 : 2)
    , recvRole_(role == CallRole::eCaller ? 2 : 1)
    , running_(false)
    , muted_(false)
    , cameraOff_(false)
    , forceKeyframe_(false)
    , sendSeqAudio_(0)
    , sendSeqVideo_(0)
    , sendSeqControl_(0)
    , videoFrameId_(0)
    , packetsSent_(0)
    , packetsReceived_(0)
{
    if (mediaKey_.size() != kAeadKeyBytes) {
        throw std::invalid_argument("CallMedia: media key must be 32 bytes");
    }
    if (videoSource_) {
        videoEncoder_.emplace(
            kCallVideoWidth, kCallVideoHeight, kCallVideoFps, kCallVideoBitrateKbps);
    }
    if (videoSink_) {
        videoDecoder_.emplace();
    }
}

CallMedia::~CallMedia()
{
    stop();
}

void CallMedia::start()
{
    if (running_.exchange(true)) {
        return;
    }
    audioCaptureThread_ = std::thread(&CallMedia::audioCaptureLoop, this);
    if (videoSource_) {
        videoCaptureThread_ = std::thread(&CallMedia::videoCaptureLoop, this);
    }
    receiveThread_ = std::thread(&CallMedia::receiveLoop, this);
}

void CallMedia::stop()
{
    if (!running_.exchange(false)) {
        return;
    }
    // Unblock the capture loops' blocking readFrame; the receive loop unblocks on
    // its own poll timeout.
    audioSource_->stop();
    if (videoSource_) {
        videoSource_->stop();
    }
    if (audioCaptureThread_.joinable()) {
        audioCaptureThread_.join();
    }
    if (videoCaptureThread_.joinable()) {
        videoCaptureThread_.join();
    }
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

void CallMedia::setMuted(const bool muted)
{
    muted_ = muted;
}

void CallMedia::setCameraEnabled(const bool enabled)
{
    cameraOff_ = !enabled;
}

bool CallMedia::hasVideo() const
{
    return videoSource_ != nullptr || videoSink_ != nullptr;
}

std::uint64_t CallMedia::packetsSent() const
{
    return packetsSent_.load(std::memory_order_relaxed);
}

std::uint64_t CallMedia::packetsReceived() const
{
    return packetsReceived_.load(std::memory_order_relaxed);
}

void CallMedia::sealAndSend(
    const std::uint8_t track, std::atomic<std::uint64_t>& counter, const Bytes& payload)
{
    const std::uint64_t sequence = counter.fetch_add(1, std::memory_order_relaxed);
    const Bytes nonce = makeNonce(sendRole_, track, sequence);
    const Bytes sealed = aeadSeal(mediaKey_, nonce, payload);

    Bytes packet;
    packet.reserve(nonce.size() + sealed.size());
    packet.insert(packet.end(), nonce.begin(), nonce.end());
    packet.insert(packet.end(), sealed.begin(), sealed.end());
    {
        // Two capture threads (and keyframe requests) share one transport.
        const std::lock_guard<std::mutex> lock(sendMutex_);
        transport_.sendDatagram(packet.data(), packet.size());
    }
    packetsSent_.fetch_add(1, std::memory_order_relaxed);
}

void CallMedia::audioCaptureLoop()
{
    audioSource_->start();
    while (running_.load()) {
        const std::vector<std::int16_t> frame = audioSource_->readFrame();
        if (frame.size() != static_cast<std::size_t>(kCallSamplesPerFrame)) {
            continue;  // stop signalled (empty) or a partial frame: skip
        }
        if (muted_.load()) {
            continue;  // transmit nothing while muted
        }
        const Bytes opus = encoder_.encode(frame.data(), static_cast<int>(frame.size()));
        sealAndSend(kTrackAudio, sendSeqAudio_, opus);
    }
}

void CallMedia::videoCaptureLoop()
{
    videoSource_->start();
    while (running_.load()) {
        const VideoFrame frame = videoSource_->readFrame();
        if (!frame.valid()) {
            continue;  // stop signalled or a bad frame
        }
        if (cameraOff_.load()) {
            continue;  // transmit nothing while the camera is off
        }
        if (frame.width != kCallVideoWidth || frame.height != kCallVideoHeight) {
            continue;  // geometry must match the encoder; skip a misconfigured frame
        }
        const bool forceKf = forceKeyframe_.exchange(false);
        const EncodedVideoFrame encoded = videoEncoder_->encode(frame, forceKf);
        if (!encoded.valid()) {
            continue;  // the codec dropped this frame
        }
        sendVideoFrame(encoded.data);
    }
    videoSource_->stop();
}

void CallMedia::sendVideoFrame(const Bytes& compressed)
{
    const std::size_t total
        = compressed.empty() ? 1 : (compressed.size() + kMaxFragmentPayload - 1) / kMaxFragmentPayload;
    if (total > kMaxFragments) {
        return;  // unreasonably large frame: drop rather than truncate the count
    }
    const std::uint32_t frameId = videoFrameId_.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t index = 0; index < total; ++index) {
        const std::size_t offset = index * kMaxFragmentPayload;
        const std::size_t length = std::min(kMaxFragmentPayload, compressed.size() - offset);
        Bytes payload;
        payload.reserve(kVideoHeaderBytes + length);
        payload.push_back(static_cast<std::uint8_t>((frameId >> 24) & 0xff));
        payload.push_back(static_cast<std::uint8_t>((frameId >> 16) & 0xff));
        payload.push_back(static_cast<std::uint8_t>((frameId >> 8) & 0xff));
        payload.push_back(static_cast<std::uint8_t>(frameId & 0xff));
        payload.push_back(static_cast<std::uint8_t>(index));
        payload.push_back(static_cast<std::uint8_t>(total));
        payload.insert(
            payload.end(), compressed.begin() + offset, compressed.begin() + offset + length);
        sealAndSend(kTrackVideo, sendSeqVideo_, payload);
    }
}

void CallMedia::receiveLoop()
{
    audioSink_->start();
    if (videoSink_) {
        videoSink_->start();
    }
    bool audioHavePlayed = false;
    std::uint64_t audioLastPlayed = 0;
    VideoReassembly reasm;
    while (running_.load()) {
        const std::vector<std::uint8_t> packet = transport_.receiveDatagram(kReceivePollMs);
        if (packet.size() <= kNonceSize) {
            continue;  // timeout or a runt with no payload
        }
        const Bytes nonce(packet.begin(), packet.begin() + kNonceSize);
        if (nonce[0] != recvRole_) {
            continue;  // not from the peer's direction
        }
        const std::uint8_t track = nonce[1];
        const Bytes sealed(packet.begin() + kNonceSize, packet.end());
        const std::optional<Bytes> opened = aeadOpen(mediaKey_, nonce, sealed);
        if (!opened.has_value()) {
            continue;  // forged, corrupt or wrong-key datagram
        }
        if (track == kTrackAudio) {
            handleAudioPacket(
                sequenceFromNonce(nonce), opened.value(), audioHavePlayed, audioLastPlayed);
        } else if (track == kTrackVideo) {
            handleVideoPacket(opened.value(), reasm);
        } else if (track == kTrackControl) {
            handleControlPacket(opened.value());
        }
    }
    audioSink_->stop();
    if (videoSink_) {
        videoSink_->stop();
    }
}

void CallMedia::handleAudioPacket(const std::uint64_t sequence, const Bytes& opus,
    bool& havePlayed, std::uint64_t& lastPlayed)
{
    if (havePlayed && sequence <= lastPlayed) {
        return;  // a late or duplicate frame: its slot has passed
    }
    if (havePlayed && sequence > lastPlayed + 1) {
        // Conceal a bounded run of missing frames so playback keeps pace.
        const std::uint64_t missing = std::min(sequence - lastPlayed - 1, kMaxPlcGap);
        for (std::uint64_t i = 0; i < missing; ++i) {
            audioSink_->writeFrame(decoder_.decode(Bytes{}));
        }
    }
    audioSink_->writeFrame(decoder_.decode(opus));
    lastPlayed = sequence;
    havePlayed = true;
    packetsReceived_.fetch_add(1, std::memory_order_relaxed);
}

void CallMedia::handleVideoPacket(const Bytes& payload, VideoReassembly& reasm)
{
    if (payload.size() < kVideoHeaderBytes) {
        return;
    }
    const std::uint32_t frameId = (static_cast<std::uint32_t>(payload[0]) << 24)
        | (static_cast<std::uint32_t>(payload[1]) << 16)
        | (static_cast<std::uint32_t>(payload[2]) << 8) | static_cast<std::uint32_t>(payload[3]);
    const std::uint8_t fragIndex = payload[4];
    const std::uint8_t fragCount = payload[5];
    if (fragCount == 0 || fragIndex >= fragCount) {
        return;
    }
    packetsReceived_.fetch_add(1, std::memory_order_relaxed);
    if (reasm.keyframeCooldown > 0) {
        --reasm.keyframeCooldown;
    }

    // Drop fragments of a frame we are past: older than the one being assembled,
    // or not newer than the last one we already decoded.
    if (reasm.active && static_cast<std::int32_t>(frameId - reasm.frameId) < 0) {
        return;
    }
    if (reasm.haveDecoded && static_cast<std::int32_t>(frameId - reasm.lastDecodedId) <= 0) {
        return;
    }

    if (!reasm.active || reasm.frameId != frameId) {
        // Switching frames; abandoning an incomplete one means we lost a fragment,
        // so ask the sender for a keyframe to resynchronise.
        if (reasm.active && reasm.haveCount < reasm.fragCount) {
            requestKeyframe(reasm);
        }
        reasm.active = true;
        reasm.frameId = frameId;
        reasm.fragCount = fragCount;
        reasm.haveCount = 0;
        reasm.frags.assign(fragCount, Bytes{});
        reasm.got.assign(fragCount, 0);
    }
    if (fragCount != reasm.fragCount) {
        return;  // inconsistent fragment count within one frame
    }
    if (reasm.got[fragIndex] == 0) {
        reasm.got[fragIndex] = 1;
        reasm.frags[fragIndex].assign(payload.begin() + kVideoHeaderBytes, payload.end());
        ++reasm.haveCount;
    }
    if (reasm.haveCount != reasm.fragCount) {
        return;  // still waiting for fragments
    }

    Bytes whole;
    for (const Bytes& fragment : reasm.frags) {
        whole.insert(whole.end(), fragment.begin(), fragment.end());
    }
    reasm.active = false;
    const VideoFrame frame = videoDecoder_->decode(whole);
    if (frame.valid()) {
        videoSink_->writeFrame(frame);
        reasm.haveDecoded = true;
        reasm.lastDecodedId = frameId;
    } else {
        requestKeyframe(reasm);  // decode produced nothing: need a fresh reference
    }
}

void CallMedia::handleControlPacket(const Bytes& payload)
{
    if (!payload.empty() && payload[0] == kCtrlRequestKeyframe) {
        forceKeyframe_.store(true);  // honoured by the next video encode (if any)
    }
}

void CallMedia::requestKeyframe(VideoReassembly& reasm)
{
    if (!videoSink_) {
        return;  // we are not decoding video, so a keyframe is not ours to ask for
    }
    if (reasm.keyframeCooldown > 0) {
        return;
    }
    reasm.keyframeCooldown = kKeyframeRequestCooldown;
    const Bytes payload = {kCtrlRequestKeyframe};
    sealAndSend(kTrackControl, sendSeqControl_, payload);
}

}  // namespace bazarish
