// Bazarish project (c) 2026
#pragma once

#include "AudioIo.hpp"

#include <QJniObject>

#include <atomic>
#include <cstdint>
#include <vector>

namespace bazarish::app {

class AndroidCallSource : public bazarish::AudioSource {
public:
    AndroidCallSource();
    ~AndroidCallSource() override;
    // The Java buffer points into frame_.
    AndroidCallSource(const AndroidCallSource&) = delete;
    AndroidCallSource& operator=(const AndroidCallSource&) = delete;

    void start() override;
    void stop() override;
    std::vector<std::int16_t> readFrame() override;

private:
    std::vector<std::int16_t> frame_;
    QJniObject buffer_;
    QJniObject capture_;
    std::atomic<bool> running_ = false;
};

class AndroidCallSink : public bazarish::AudioSink {
public:
    AndroidCallSink();
    ~AndroidCallSink() override;
    // The Java buffer points into frame_.
    AndroidCallSink(const AndroidCallSink&) = delete;
    AndroidCallSink& operator=(const AndroidCallSink&) = delete;

    void start() override;
    void stop() override;
    void writeFrame(const std::vector<std::int16_t>& pcm) override;

protected:
    explicit AndroidCallSink(const char* playbackClass);

private:
    const char* const playbackClass_;
    std::vector<std::int16_t> frame_;
    QJniObject buffer_;
    QJniObject playback_;
    std::atomic<bool> running_ = false;
};

// A voice note: aloud, and through the earpiece while the phone is at the ear.
class AndroidVoiceSink : public AndroidCallSink {
public:
    AndroidVoiceSink();
};

namespace AndroidCallAudio {
void setSpeaker(bool on);
void holdNearEar(bool on);
}

}  // namespace bazarish::app
