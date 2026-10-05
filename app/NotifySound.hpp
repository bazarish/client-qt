// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class QAudioOutput;
class QMediaPlayer;

namespace bazarish::app {

class NotifySound : public QObject {
public:
    NotifySound(QString folder, QString fileName, QObject* parent = nullptr);
    ~NotifySound() override;

    void play();
    qint64 durationMs() const;

private:
    QUrl source() const;

    const QString folder_;
    const QString fileName_;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> output_;
};

}  // namespace bazarish::app
