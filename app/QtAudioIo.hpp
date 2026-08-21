// Bazarish project (c) 2026
#pragma once

#include "AudioIo.hpp"

#include <QAudioFormat>

#include <cstdint>
#include <memory>
#include <vector>

class QAudioSource;
class QAudioSink;

namespace bazarish::app {

// The shared call audio format: 48 kHz mono signed-16, matching the Opus codec.
// A sink may be opened at a higher rate to play the same samples faster (see
// QtAudioSink).
QAudioFormat callAudioFormat(int sampleRate = kCallSampleRate);

// Microphone capture via Qt Multimedia. QAudioSource runs in pull mode, writing
// captured PCM into an internal thread-safe ring; the call engine's capture
// thread pops 20 ms frames from it. The QAudioSource itself is only ever touched
// on the thread that constructs this object (the session worker thread).
class QtAudioSource : public bazarish::AudioSource {
public:
    QtAudioSource();
    ~QtAudioSource() override;

    void start() override;
    void stop() override;
    std::vector<std::int16_t> readFrame() override;

private:
    class CaptureDevice;
    std::unique_ptr<QAudioSource> source_;
    std::unique_ptr<CaptureDevice> device_;
};

// Speaker playback via Qt Multimedia. QAudioSink runs in pull mode, reading PCM
// from an internal thread-safe ring the call engine's receive thread fills;
// underruns play silence. Same thread-affinity rule as QtAudioSource.
class QtAudioSink : public bazarish::AudioSink {
public:
    // Opening the device above the recording rate is how playback speeds up:
    // the same samples are consumed faster, and the pitch rises with them.
    explicit QtAudioSink(int sampleRate = kCallSampleRate);
    ~QtAudioSink() override;

    void start() override;
    void stop() override;
    void writeFrame(const std::vector<std::int16_t>& pcm) override;

private:
    class PlaybackDevice;
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<PlaybackDevice> device_;
};

}  // namespace bazarish::app
