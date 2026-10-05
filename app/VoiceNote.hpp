// Bazarish project (c) 2026
#pragma once

#include "AudioCodec.hpp"
#include "AudioIo.hpp"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace bazarish::app {

class VoiceNote : public QObject {
    Q_OBJECT
public:
    explicit VoiceNote(QObject* parent = nullptr);
    ~VoiceNote() override;

    void startRecording();
    void startMonitoring();
    void stopMonitoring();
    Bytes stopRecording();
    void cancelRecording();
    bool recording() const { return recording_.load(); }
    qint64 elapsedMs() const;
    float inputLevel() const { return inputLevel_.load(); }
    std::size_t encodedBytes() const { return encodedBytes_.load(); }

    void play(const Bytes& opus, double speed = 1.0, qint64 fromMs = 0);
    void stop();
    bool playing() const { return playing_.load(); }
    qint64 playbackPositionMs() const { return playedMs_.load(); }

signals:
    void playbackFinished();

private:
    void stopCaptureThread();
    void stopPlaybackThread();

    std::unique_ptr<bazarish::AudioSource> source_;
    std::unique_ptr<bazarish::AudioSink> sink_;
    std::thread captureThread_;
    std::thread playbackThread_;
    std::atomic<bool> recording_{false};
    std::atomic<bool> playing_{false};
    void begin(bool monitorOnly);

    std::atomic<float> inputLevel_{0.0F};
    std::atomic<bool> monitorOnly_{false};
    std::atomic<std::size_t> encodedBytes_{0};
    std::atomic<qint64> playedMs_{0};
    std::vector<std::int16_t> pcm_;
    std::mutex pcmMutex_;
    qint64 startedAtMs_ = 0;
};

}  // namespace bazarish::app
