// Bazarish project (c) 2026
#pragma once

#include <QObject>

#include <memory>

class QAudioOutput;
class QMediaPlayer;

namespace bazarish::app {

// The sound an incoming call makes: the carried ringtone, repeated until the
// call is answered, declined or given up on. Not the notification sound - a
// message announces itself once and is still there afterwards, while a call is
// only there while it rings.
// Lives on the thread that owns it, which must run an event loop.
class Ringtone : public QObject {
public:
    explicit Ringtone(QObject* parent = nullptr);
    ~Ringtone() override;

    // Starts it, or leaves it running if it already is: a call that is ringing
    // does not start ringing again on every republished state.
    void start();
    void stop();

private:
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> output_;
};

}  // namespace bazarish::app
