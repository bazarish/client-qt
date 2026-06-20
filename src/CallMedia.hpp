// Bazarish project (c) 2026
#pragma once

#include "AudioIo.hpp"

#include <bazarish/Bytes.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace bazarish {

class SamDatagramSession;

// Per-call media datagram transport to one fixed peer. Abstract so the engine
// runs over an in-memory loopback in tests and over SAM in production.
class CallTransport {
public:
    virtual ~CallTransport() = default;
    virtual void sendDatagram(const void* data, std::size_t size) = 0;
    // Returns one datagram payload, or empty on timeout (timeoutMs, negative
    // blocks). The receive loop polls so it can observe a stop request.
    virtual std::vector<std::uint8_t> receiveDatagram(int timeoutMs) = 0;
};

// Routes media over a SAM RAW datagram session to one fixed peer destination
// (a base64 destination or a .b32.i2p host).
class SamCallTransport : public CallTransport {
public:
    SamCallTransport(SamDatagramSession& session, std::string peerDestination);
    void sendDatagram(const void* data, std::size_t size) override;
    std::vector<std::uint8_t> receiveDatagram(int timeoutMs) override;

private:
    SamDatagramSession& session_;
    std::string peerDestination_;
};

// Which side of the call this engine is. The role selects the AES-GCM nonce
// direction prefix so the two media streams never reuse a (key, nonce) pair.
enum class CallRole {
    eCaller,
    eCallee,
};

// Real-time audio engine for a single call. Two worker threads: capture
// (microphone -> Opus -> AES-256-GCM -> transport) and receive (transport ->
// decrypt -> Opus -> speaker). Best-effort, RTP-style: lost frames are
// concealed by Opus, late/duplicate frames are dropped. Every datagram is
// AES-256-GCM sealed with the per-call key, so a forged or replayed datagram is
// silently dropped even though the RAW session itself is unauthenticated.
class CallMedia {
public:
    CallMedia(CallTransport& transport, std::unique_ptr<AudioSource> source,
        std::unique_ptr<AudioSink> sink, const Bytes& mediaKey, CallRole role);
    ~CallMedia();

    CallMedia(const CallMedia&) = delete;
    CallMedia& operator=(const CallMedia&) = delete;

    void start();
    void stop();
    // While muted the capture loop keeps running but transmits nothing (the
    // peer hears concealment silence).
    void setMuted(bool muted);

    std::uint64_t packetsSent() const;
    std::uint64_t packetsReceived() const;

private:
    void captureLoop();
    void receiveLoop();

    CallTransport& transport_;
    std::unique_ptr<AudioSource> source_;
    std::unique_ptr<AudioSink> sink_;
    Bytes mediaKey_;
    std::uint8_t sendRole_;
    std::uint8_t recvRole_;

    std::atomic<bool> running_;
    std::atomic<bool> muted_;
    std::atomic<std::uint64_t> sendSeq_;
    std::atomic<std::uint64_t> packetsSent_;
    std::atomic<std::uint64_t> packetsReceived_;

    std::thread captureThread_;
    std::thread receiveThread_;
    AudioEncoder encoder_;
    AudioDecoder decoder_;
};

}  // namespace bazarish
