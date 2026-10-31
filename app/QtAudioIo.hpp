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

QAudioFormat callAudioFormat();

class QtAudioSource : public bazarish::AudioSource {
public:
    QtAudioSource();
    ~QtAudioSource() override;

    void start() override;
    void stop() override;
    std::vector<std::int16_t> readFrame() override;

private:
    class CaptureDevice;
    void open();
    void follow();
    std::unique_ptr<QAudioSource> source_;
    std::unique_ptr<CaptureDevice> device_;
};

class QtAudioSink : public bazarish::AudioSink {
public:
    QtAudioSink();
    ~QtAudioSink() override;

    void start() override;
    void stop() override;
    void writeFrame(const std::vector<std::int16_t>& pcm) override;

private:
    class PlaybackDevice;
    void open();
    void follow();
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<PlaybackDevice> device_;
};

}  // namespace bazarish::app
