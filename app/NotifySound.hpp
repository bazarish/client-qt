// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class QAudioOutput;
class QMediaPlayer;

namespace bazarish::app {

// A sound a notification makes. The application carries one of each; a file of
// the same name at the root of the installation takes its place. The folder is
// consulted on every play, so a file put there is used without restarting.
// Lives on the thread that owns it, which must run an event loop.
//
// One class, two sounds: a message is announced with `notify.wav` and a reaction
// to one with the shorter, quieter `reaction.wav`. What differs between them is
// the file, so that is what the caller names - two of these rather than two
// classes that would only differ in a string.
class NotifySound : public QObject {
public:
    // fileName is both the name inside the application and the name a user's own
    // recording goes by at the root of the installation.
    NotifySound(QString folder, QString fileName, QObject* parent = nullptr);
    ~NotifySound() override;

    void play();
    // How long the sound in use runs, in milliseconds; 0 until the player has
    // read the file. The caller spaces sounds by it, so it belongs here as a fact
    // about the sound rather than as a number written somewhere else.
    qint64 durationMs() const;

private:
    // The recording of the user's own if there is one, the built-in otherwise.
    QUrl source() const;

    const QString folder_;
    const QString fileName_;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> output_;
};

}  // namespace bazarish::app
