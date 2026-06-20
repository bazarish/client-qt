// Bazarish project (c) 2026
#include "VideoIo.hpp"

#include <chrono>
#include <thread>

namespace bazarish {

PatternVideoSource::PatternVideoSource(const int width, const int height, const int fps)
    : width_(width)
    , height_(height)
    , fps_(fps > 0 ? fps : kCallVideoFps)
    , counter_(0)
    , running_(false)
{
}

void PatternVideoSource::start()
{
    running_ = true;
}

void PatternVideoSource::stop()
{
    running_ = false;
}

VideoFrame PatternVideoSource::readFrame()
{
    if (!running_) {
        return VideoFrame{};
    }
    // Pace at the frame interval so the synthetic camera produces real-time video.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps_));
    if (!running_) {
        return VideoFrame{};
    }
    VideoFrame frame;
    frame.width = width_;
    frame.height = height_;
    frame.i420.resize(i420Size(width_, height_));
    const std::uint32_t t = counter_++;
    // Luma: a diagonal ramp that shifts each frame, so motion is present and the
    // image is non-uniform (a uniform image would not exercise the codec).
    unsigned char* p = frame.i420.data();
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            *p++ = static_cast<unsigned char>((x + y + t) & 0xff);
        }
    }
    // Chroma: neutral grey (128) so the synthetic frame is a clean greyscale ramp.
    const std::size_t chroma = frame.i420.size() - static_cast<std::size_t>(width_) * height_;
    for (std::size_t i = 0; i < chroma; ++i) {
        *p++ = 128;
    }
    return frame;
}

CapturingVideoSink::CapturingVideoSink(const bool retain)
    : retain_(retain)
    , frameCount_(0)
{
}

void CapturingVideoSink::start()
{
}

void CapturingVideoSink::stop()
{
}

void CapturingVideoSink::writeFrame(const VideoFrame& frame)
{
    frameCount_.fetch_add(1, std::memory_order_relaxed);
    if (retain_) {
        const std::lock_guard<std::mutex> lock(mutex_);
        lastFrame_ = frame;
    }
}

std::uint64_t CapturingVideoSink::frameCount() const
{
    return frameCount_.load(std::memory_order_relaxed);
}

VideoFrame CapturingVideoSink::lastFrame() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return lastFrame_;
}

}  // namespace bazarish
