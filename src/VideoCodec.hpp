// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <cstdint>

namespace bazarish {

// Call video format. Resolution and rate are deliberately low: the media rides
// I2P datagram tunnels whose throughput is bounded by the slowest relay hop, so
// a small, loss-tolerant stream is the realistic target. These are defaults; the
// encoder takes the actual geometry so the camera backend can pick another size.
inline constexpr int kCallVideoWidth = 320;
inline constexpr int kCallVideoHeight = 240;
inline constexpr int kCallVideoFps = 15;
inline constexpr int kCallVideoBitrateKbps = 256;

// One raw video frame in planar I420 (YUV 4:2:0): a Y plane of width*height
// bytes, then U and V planes of (width/2)*(height/2) bytes each. This is VP8's
// native pixel format, so capture and playback convert to/from I420 once at the
// edges and the codec copies nothing extra.
struct VideoFrame {
    int width = 0;
    int height = 0;
    Bytes i420;

    bool valid() const { return width > 0 && height > 0 && !i420.empty(); }
};

// Size in bytes of a packed I420 buffer for the given geometry.
std::size_t i420Size(int width, int height);

// One compressed VP8 frame produced by the encoder.
struct EncodedVideoFrame {
    Bytes data;
    bool keyframe = false;
    bool valid() const { return !data.empty(); }
};

// VP8 encoder for one realtime video stream. Move-only; owns the codec state.
// Configured for low-latency one-pass realtime with error resilience (so the
// decoder recovers from datagram loss without waiting for a full keyframe) and
// no auto alt-ref (exactly one compressed frame per encode, lower latency).
class VideoEncoder {
public:
    VideoEncoder(int width, int height, int fps, int bitrateKbps);
    ~VideoEncoder();

    VideoEncoder(VideoEncoder&& other) noexcept;
    VideoEncoder& operator=(VideoEncoder&& other) noexcept;
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // Encodes one I420 frame. forceKeyframe makes the output a keyframe (used to
    // answer a peer's keyframe request after loss). Returns an empty result if
    // the codec dropped the frame.
    EncodedVideoFrame encode(const VideoFrame& frame, bool forceKeyframe);

private:
    int width_;
    int height_;
    void* codec_;  // vpx_codec_ctx_t*, opaque to avoid leaking libvpx into headers
    void* image_;  // vpx_image_t*, the reusable I420 input wrapper
    std::int64_t pts_;
};

// VP8 decoder for one realtime video stream. Move-only; owns the codec state.
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(VideoDecoder&& other) noexcept;
    VideoDecoder& operator=(VideoDecoder&& other) noexcept;
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Decodes one compressed VP8 frame into an I420 VideoFrame. Returns an
    // invalid frame if the packet produced no output (for example a non-keyframe
    // arriving before the decoder has a reference).
    VideoFrame decode(const Bytes& compressed);

private:
    void* codec_;  // vpx_codec_ctx_t*
};

}  // namespace bazarish
