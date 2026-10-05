// Bazarish project (c) 2026
#pragma once

#include <QObject>

#include <memory>

class QAudioSink;

namespace bazarish::app {

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

    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<Voice> voice_;
};

}  // namespace bazarish::app
