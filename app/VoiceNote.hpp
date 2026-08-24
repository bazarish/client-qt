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
    // Stops and hands over what was captured, levelled and encoded. The audio is
    // encoded here rather than while it is captured: the gain that levels a
    // recording is only knowable once all of it is in hand.
    Bytes stopRecording();
    void cancelRecording();
    bool recording() const { return recording_.load(); }
    // How long the recording has been running.
    qint64 elapsedMs() const;
    // Loudness of the last frame off the microphone, 0..1. A microphone that is
    // not delivering audio holds this at zero, which is what makes a dead one
    // visible instead of merely silent.
    float inputLevel() const { return inputLevel_.load(); }
    // How much the recording will weigh, encoded. A voice message rides inside
    // one message, so this - not the clock - is what bounds it. An estimate from
    // the bitrate the recorder asks for, because nothing is encoded until the
    // recording ends.
    std::size_t encodedBytes() const { return encodedBytes_.load(); }

    // Plays a recorded run of frames at `speed` (1.0 is as recorded), starting
    // fromMs into the recording. Playing again while one is running replaces it:
    // two voices at once is nobody's intent.
    void play(const Bytes& opus, double speed = 1.0, qint64 fromMs = 0);
    void stop();
    bool playing() const { return playing_.load(); }
    // How far into the recording playback has reached, in recording time - so it
    // is the same number whatever speed it is being played at.
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
    std::atomic<float> inputLevel_{0.0F};
    std::atomic<std::size_t> encodedBytes_{0};
    std::atomic<qint64> playedMs_{0};
    // The recording as captured, one run of samples; encoded on stop.
    std::vector<std::int16_t> pcm_;
    std::mutex pcmMutex_;
    qint64 startedAtMs_ = 0;
};

}  // namespace bazarish::app
