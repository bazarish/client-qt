// Bazarish project (c) 2026
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class QSoundEffect;

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
    std::unique_ptr<QSoundEffect> effect_;
    QElapsedTimer playing_;
    qint64 durationMs_ = 0;
};

}  // namespace bazarish::app
