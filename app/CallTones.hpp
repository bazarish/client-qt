// Bazarish project (c) 2026
#pragma once

#include <QObject>

#include <memory>

class QAudioSink;
class QTimer;

namespace bazarish::app {

class AndroidCallSink;

class CallTones : public QObject {
public:
    explicit CallTones(QObject* parent = nullptr);
    ~CallTones() override;

    void ringback();
    void failure();
    void endRingback();
    void stop();

private:
    class Voice;

    void play(int onMs, int offMs, int bursts);
    void feed();

    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<Voice> voice_;
#ifdef Q_OS_ANDROID
    std::unique_ptr<AndroidCallSink> phone_;
    std::unique_ptr<QTimer> feeder_;
#endif
};

}  // namespace bazarish::app
