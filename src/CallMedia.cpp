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
// sealed Opus packet (ciphertext || 16-byte tag). The nonce is
// role(1) || 0 0 0 || sequence(8, big-endian): the role byte separates the two
// call directions and the per-sender monotonic sequence guarantees uniqueness,
// so a (key, nonce) pair is never reused.
constexpr std::size_t kNonceSize = kAeadNonceBytes;  // 12

// A jump larger than this many lost frames is treated as a fresh start rather
// than synthesising a long burst of concealment audio.
constexpr std::uint64_t kMaxPlcGap = 5;

// How long the receive loop waits per poll before re-checking the stop flag.
constexpr int kReceivePollMs = 200;

Bytes makeNonce(const std::uint8_t role, const std::uint64_t sequence)
{
    Bytes nonce(kNonceSize, 0);
    nonce[0] = role;
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

CallMedia::CallMedia(CallTransport& transport, std::unique_ptr<AudioSource> source,
    std::unique_ptr<AudioSink> sink, const Bytes& mediaKey, const CallRole role)
    : transport_(transport)
    , source_(std::move(source))
    , sink_(std::move(sink))
    , mediaKey_(mediaKey)
    , sendRole_(role == CallRole::eCaller ? 1 : 2)
    , recvRole_(role == CallRole::eCaller ? 2 : 1)
    , running_(false)
    , muted_(false)
    , sendSeq_(0)
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
    captureThread_ = std::thread(&CallMedia::captureLoop, this);
    receiveThread_ = std::thread(&CallMedia::receiveLoop, this);
}

void CallMedia::stop()
{
    if (!running_.exchange(false)) {
        return;
    }
    // Unblock the capture loop's readFrame; the receive loop unblocks on its
    // own poll timeout.
    source_->stop();
    if (captureThread_.joinable()) {
        captureThread_.join();
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

std::uint64_t CallMedia::packetsReceived() const
{
    return packetsReceived_.load(std::memory_order_relaxed);
}

void CallMedia::captureLoop()
{
    source_->start();
    while (running_.load()) {
        const std::vector<std::int16_t> frame = source_->readFrame();
        if (frame.size() != static_cast<std::size_t>(kCallSamplesPerFrame)) {
            continue;  // stop signalled (empty) or a partial frame: skip
        }
        if (muted_.load()) {
            continue;  // transmit nothing while muted
        }
        const Bytes opus = encoder_.encode(frame.data(), static_cast<int>(frame.size()));
        const std::uint64_t sequence = sendSeq_.fetch_add(1, std::memory_order_relaxed);
        const Bytes nonce = makeNonce(sendRole_, sequence);
        const Bytes sealed = aeadSeal(mediaKey_, nonce, opus);

        Bytes packet;
        packet.reserve(nonce.size() + sealed.size());
        packet.insert(packet.end(), nonce.begin(), nonce.end());
        packet.insert(packet.end(), sealed.begin(), sealed.end());
        transport_.sendDatagram(packet.data(), packet.size());
        packetsSent_.fetch_add(1, std::memory_order_relaxed);
    }
}

void CallMedia::receiveLoop()
{
    sink_->start();
    bool havePlayed = false;
    std::uint64_t lastPlayed = 0;
    while (running_.load()) {
        const std::vector<std::uint8_t> packet = transport_.receiveDatagram(kReceivePollMs);
        if (packet.size() <= kNonceSize) {
            continue;  // timeout or a runt with no payload
        }
        const Bytes nonce(packet.begin(), packet.begin() + kNonceSize);
        if (nonce[0] != recvRole_) {
            continue;  // not from the peer's direction
        }
        const Bytes sealed(packet.begin() + kNonceSize, packet.end());
        const std::optional<Bytes> opus = aeadOpen(mediaKey_, nonce, sealed);
        if (!opus.has_value()) {
            continue;  // forged, corrupt or wrong-key datagram
        }
        const std::uint64_t sequence = sequenceFromNonce(nonce);
        if (havePlayed && sequence <= lastPlayed) {
            continue;  // a late or duplicate frame: its slot has passed
        }
        if (havePlayed && sequence > lastPlayed + 1) {
            // Conceal a bounded run of missing frames so playback keeps pace.
            const std::uint64_t missing = std::min(sequence - lastPlayed - 1, kMaxPlcGap);
            for (std::uint64_t i = 0; i < missing; ++i) {
                sink_->writeFrame(decoder_.decode(Bytes{}));
            }
        }
        sink_->writeFrame(decoder_.decode(opus.value()));
        lastPlayed = sequence;
        havePlayed = true;
        packetsReceived_.fetch_add(1, std::memory_order_relaxed);
    }
    sink_->stop();
}

}  // namespace bazarish
