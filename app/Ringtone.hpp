// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class QAudioSink;
class QTimer;

namespace bazarish::app {

class Ringtone : public QObject {
    Q_OBJECT
public:
    explicit Ringtone(QString folder, QObject* parent = nullptr);
    ~Ringtone() override;

    void start();
    void stop();

signals:
    void levelChanged(qreal level);

private:
    class Loop;

    bool loadTrack();
    bool loadFrom(const QString& path);
    void publishLevel();

    const QString folder_;
    std::vector<std::int16_t> samples_;
    std::vector<float> envelope_;
    std::size_t frameSamples_ = 0;
    int sampleRate_ = 0;

    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<Loop> loop_;
    std::unique_ptr<QTimer> levelTimer_;
};

}  // namespace bazarish::app
