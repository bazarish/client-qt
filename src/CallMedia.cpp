// Bazarish project (c) 2026
#include "CallMedia.hpp"

#include <bazarish/Crypto.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace bazarish {

namespace {

// Datagram wire format: a 12-byte AES-GCM nonce (cleartext) followed by the
// sealed payload (ciphertext || 16-byte tag). The nonce is
// role(1) || track(1) || 0 0 || sequence(8, big-endian).
constexpr std::size_t kNonceSize = kAeadNonceBytes;  // 12

// Track byte (nonce[1]): which media stream a datagram carries. Audio is the
// only one; the byte stays in the nonce so a second track can be added without
// reusing a (key, nonce) pair.
constexpr std::uint8_t kTrackAudio = 0;

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

I2pCallTransport::I2pCallTransport(bazarish::i2p::Endpoint& endpoint, std::string peerDestination)
    : endpoint_(endpoint)
    , peerDestination_(std::move(peerDestination))
{
}

void I2pCallTransport::sendDatagram(const void* data, const std::size_t size)
{
    endpoint_.sendRawDatagram(peerDestination_, data, size);
}

std::vector<std::uint8_t> I2pCallTransport::receiveDatagram(const int timeoutMs)
{
    // A negative timeout means block; map it to a long poll so the engine's stop
    // flag is still observed between polls.
    const auto wait = timeoutMs < 0 ? std::chrono::milliseconds(1000)
                                     : std::chrono::milliseconds(timeoutMs);
    return endpoint_.receiveRawDatagram(wait);
}

CallMedia::CallMedia(CallTransport& transport, std::unique_ptr<AudioSource> audioSource,
    std::unique_ptr<AudioSink> audioSink, const Bytes& mediaKey, const CallRole role)
    : transport_(transport)
    , audioSource_(std::move(audioSource))
    , audioSink_(std::move(audioSink))
    , mediaKey_(mediaKey)
    , sendRole_(role == CallRole::eCaller ? 1 : 2)
    , recvRole_(role == CallRole::eCaller ? 2 : 1)
    , running_(false)
    , muted_(false)
    , sendSeqAudio_(0)
    , packetsSent_(0)
    , packetsReceived_(0)
{
    if (mediaKey_.size() != kAeadKeyBytes) {
        throw std::invalid_argument("CallMedia: media key must be 32 bytes");
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
    if (audioCaptureThread_.joinable()) {
        audioCaptureThread_.join();
    }
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

void CallMedia::setMuted(const bool muted)
{
    muted_ = muted;
}

std::uint64_t CallMedia::packetsSent() const
{
    return packetsSent_.load(std::memory_order_relaxed);
}

void CallMedia::setOnFirstPacket(std::function<void()> callback)
{
    onFirstPacket_ = std::move(callback);
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

void CallMedia::receiveLoop()
{
    audioSink_->start();
    bool audioHavePlayed = false;
    std::uint64_t audioLastPlayed = 0;
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
        }
    }
    audioSink_->stop();
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
    if (packetsReceived_.fetch_add(1, std::memory_order_relaxed) == 0 && onFirstPacket_) {
        onFirstPacket_();  // media is flowing: this is where the call really starts
    }
}


}  // namespace bazarish
