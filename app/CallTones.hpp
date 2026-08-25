// Bazarish project (c) 2026
#pragma once

#include <QObject>

#include <memory>

class QAudioSink;

namespace bazarish::app {

// Call-progress tones, in the shape telephony has taught everyone to read: a long
// tone with a long gap while the channel is being opened, a short repeated burst
// when the call never happened. Silence while the invitation is still travelling -
// there is nothing to report yet.
// Lives on the thread that owns it, which must run an event loop.
class CallTones : public QObject {
public:
    explicit CallTones(QObject* parent = nullptr);
    ~CallTones() override;

    // Long tone, long pause, until something stops it.
    void ringback();
    // A few short bursts, then silence on its own.
    void failure();
    // Ends the ringback; a burst already playing is left to finish, so it does not
    // matter whether the call ended before or after the outcome was known.
    void endRingback();
    void stop();

private:
    class Voice;

    void play(int onMs, int offMs, int bursts);

    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<Voice> voice_;
};

}  // namespace bazarish::app
