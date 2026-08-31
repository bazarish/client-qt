// Bazarish project (c) 2026
#pragma once

#include <QObject>

#include <cstdint>
#include <memory>
#include <vector>

class QAudioSink;
class QTimer;

namespace bazarish::app {

// The sound an incoming call makes: the carried ringtone, repeated until the
// call is answered, declined or given up on. Not the notification sound - a
// message announces itself once and is still there afterwards, while a call is
// only there while it rings.
//
// It plays the samples itself rather than handing the file to a player, because
// the interface pulses with the sound: that needs the loudness of what is being
// heard at this instant, which a player will not tell.
// Lives on the thread that owns it, which must run an event loop.
class Ringtone : public QObject {
    Q_OBJECT
public:
    explicit Ringtone(QObject* parent = nullptr);
    ~Ringtone() override;

    // Starts it, or leaves it running if it already is: a call that is ringing
    // does not start ringing again on every republished state.
    void start();
    void stop();

signals:
    // How loud the ringtone is right now, 0 to 1, taken from how much of it has
    // actually been played rather than from how much has been handed over.
    void levelChanged(qreal level);

private:
    class Loop;

    // Reads the carried track once. False (with a line in the log) when what was
    // packed is not the audio this expects, which leaves the call silent.
    bool loadTrack();
    void publishLevel();

    std::vector<std::int16_t> samples_;
    // The loudness of the track in fixed-length frames, 0 to 1, one entry per
    // frame from its start.
    std::vector<float> envelope_;
    int sampleRate_ = 0;

    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<Loop> loop_;
    std::unique_ptr<QTimer> levelTimer_;
};

}  // namespace bazarish::app
