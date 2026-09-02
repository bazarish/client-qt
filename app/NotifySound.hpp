// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class QAudioOutput;
class QMediaPlayer;

namespace bazarish::app {

// The sound a notification makes. The application carries one; a "notify.wav" at
// the root of the installation takes its place. The folder is consulted on every
// play, so a file put there is used without restarting.
// Lives on the thread that owns it, which must run an event loop.
class NotifySound : public QObject {
public:
    explicit NotifySound(QString folder, QObject* parent = nullptr);
    ~NotifySound() override;

    void play();

private:
    // The recording of the user's own if there is one, the built-in otherwise.
    QUrl source() const;

    const QString folder_;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> output_;
};

}  // namespace bazarish::app
