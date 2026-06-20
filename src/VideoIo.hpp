// Bazarish project (c) 2026
#pragma once

#include "VideoCodec.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace bazarish {

// Camera abstraction: the call engine pulls one I420 frame at a time. readFrame
// blocks until a frame is available (self-paced at the capture rate) and returns
// an invalid frame once stopped, so the engine's capture loop needs no clock of
// its own. The real backend (Qt Multimedia) lives in the GUI; the lib ships only
// a device-free backend so the pipeline is testable headless.
class VideoSource {
public:
    virtual ~VideoSource() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual VideoFrame readFrame() = 0;
};

// Display abstraction: the call engine pushes decoded I420 frames.
class VideoSink {
public:
    virtual ~VideoSink() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void writeFrame(const VideoFrame& frame) = 0;
};

// A device-free source that synthesises a moving pattern, paced at the frame
// rate. Stands in for a camera on headless builds and integration tests (a
// deterministic image whose motion survives the VP8 round trip).
class PatternVideoSource : public VideoSource {
public:
    PatternVideoSource(int width = kCallVideoWidth, int height = kCallVideoHeight,
        int fps = kCallVideoFps);
    void start() override;
    void stop() override;
    VideoFrame readFrame() override;

private:
    int width_;
    int height_;
    int fps_;
    std::uint32_t counter_;
    std::atomic<bool> running_;
};

// A sink that counts and (optionally) retains the most recent frame, for
// headless runs and tests. Thread-safe; the call engine writes from its receive
// thread.
class CapturingVideoSink : public VideoSink {
public:
    explicit CapturingVideoSink(bool retain = false);
    void start() override;
    void stop() override;
    void writeFrame(const VideoFrame& frame) override;

    std::uint64_t frameCount() const;
    VideoFrame lastFrame() const;

private:
    bool retain_;
    std::atomic<std::uint64_t> frameCount_;
    mutable std::mutex mutex_;
    VideoFrame lastFrame_;
};

}  // namespace bazarish
