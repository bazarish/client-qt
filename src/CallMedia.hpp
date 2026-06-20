// Bazarish project (c) 2026
#pragma once

#include "AudioIo.hpp"
#include "VideoIo.hpp"

#include <bazarish/Bytes.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

// Real-time media engine for a single call. Audio always runs; video is optional
// (null video source/sink -> audio-only). Worker threads: audio capture
// (microphone -> Opus -> seal -> transport), video capture (camera -> VP8 ->
// fragment -> seal -> transport, only when a video source is given), and one
// receive thread that demultiplexes incoming datagrams by track and routes them
// to audio playback or video reassembly/decode/render.
//
// Every datagram is AES-256-GCM sealed with the per-call key. The 12-byte nonce
// is role(1) || track(1) || 0 0 || sequence(8, big-endian): role separates the
// two directions, track separates audio/video/control, and the per-(role,track)
// monotonic sequence guarantees a (key, nonce) pair is never reused. A forged or
// replayed datagram fails to open and is dropped. Audio fits one datagram; a
// video frame is fragmented across datagrams and reassembled by the receiver,
// which asks the sender for a fresh keyframe (a control datagram) after loss.
class CallMedia {
public:
    CallMedia(CallTransport& transport, std::unique_ptr<AudioSource> audioSource,
        std::unique_ptr<AudioSink> audioSink, std::unique_ptr<VideoSource> videoSource,
        std::unique_ptr<VideoSink> videoSink, const Bytes& mediaKey, CallRole role);
    ~CallMedia();

    CallMedia(const CallMedia&) = delete;
    CallMedia& operator=(const CallMedia&) = delete;

    void start();
    void stop();
    // While muted the audio capture loop keeps running but transmits nothing (the
    // peer hears concealment silence).
    void setMuted(bool muted);
    // Enables/disables the local camera. While disabled the video capture loop
    // keeps running but transmits nothing (the peer's last frame freezes).
    void setCameraEnabled(bool enabled);

    bool hasVideo() const;

    std::uint64_t packetsSent() const;
    std::uint64_t packetsReceived() const;

private:
    // Reassembly state for the inbound video track, owned by the receive thread.
    struct VideoReassembly {
        bool active = false;
        std::uint32_t frameId = 0;
        std::uint8_t fragCount = 0;
        std::uint8_t haveCount = 0;
        std::vector<Bytes> frags;
        std::vector<char> got;
        bool haveDecoded = false;
        std::uint32_t lastDecodedId = 0;
        int keyframeCooldown = 0;
    };

    void audioCaptureLoop();
    void videoCaptureLoop();
    void receiveLoop();

    void sealAndSend(std::uint8_t track, std::atomic<std::uint64_t>& counter, const Bytes& payload);
    void sendVideoFrame(const Bytes& compressed);
    void handleAudioPacket(
        std::uint64_t sequence, const Bytes& opus, bool& havePlayed, std::uint64_t& lastPlayed);
    void handleVideoPacket(const Bytes& payload, VideoReassembly& reasm);
    void handleControlPacket(const Bytes& payload);
    void requestKeyframe(VideoReassembly& reasm);

    CallTransport& transport_;
    std::unique_ptr<AudioSource> audioSource_;
    std::unique_ptr<AudioSink> audioSink_;
    std::unique_ptr<VideoSource> videoSource_;
    std::unique_ptr<VideoSink> videoSink_;
    Bytes mediaKey_;
    std::uint8_t sendRole_;
    std::uint8_t recvRole_;

    std::atomic<bool> running_;
    std::atomic<bool> muted_;
    std::atomic<bool> cameraOff_;
    std::atomic<bool> forceKeyframe_;
    std::atomic<std::uint64_t> sendSeqAudio_;
    std::atomic<std::uint64_t> sendSeqVideo_;
    std::atomic<std::uint64_t> sendSeqControl_;
    std::atomic<std::uint32_t> videoFrameId_;
    std::atomic<std::uint64_t> packetsSent_;
    std::atomic<std::uint64_t> packetsReceived_;

    std::mutex sendMutex_;

    std::thread audioCaptureThread_;
    std::thread videoCaptureThread_;
    std::thread receiveThread_;
    AudioEncoder encoder_;
    AudioDecoder decoder_;
    std::optional<VideoEncoder> videoEncoder_;
    std::optional<VideoDecoder> videoDecoder_;
};

}  // namespace bazarish
