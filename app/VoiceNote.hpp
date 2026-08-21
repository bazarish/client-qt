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

// Recording and playing a voice message: the same Opus codec and the same 48 kHz
// mono 20 ms frames a call uses, so a voice note is a call's audio without the
// call. The bytes never touch the disk - they go from the microphone into the
// message, and from the message into the speaker.
class VoiceNote : public QObject {
    Q_OBJECT
public:
    explicit VoiceNote(QObject* parent = nullptr);
    ~VoiceNote() override;

    // Starts capturing. Throws when there is no microphone to capture from.
    void startRecording();
    // Stops and hands over what was captured, encoded.
    Bytes stopRecording();
    void cancelRecording();
    bool recording() const { return recording_.load(); }
    // How long the recording has been running.
    qint64 elapsedMs() const;

    // Plays a recorded run of frames. Playing again while one is running replaces
    // it: two voices at once is nobody's intent.
    void play(const Bytes& opus);
    void stop();
    bool playing() const { return playing_.load(); }

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
    std::vector<Bytes> frames_;
    std::mutex framesMutex_;
    qint64 startedAtMs_ = 0;
};

}  // namespace bazarish::app
