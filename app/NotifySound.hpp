// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QAudioOutput;
class QAudioSink;
class QBuffer;
class QMediaPlayer;

namespace bazarish::app {

// The sound a notification makes. A recording dropped into the account folder as
// "notify" (wav, ogg, opus, flac or mp3) is played if there is one; otherwise a
// short, soft hiss rendered here - at a level that carries across a room without
// taking it over. The folder is consulted on every play, so a file put there is
// used without restarting.
// Lives on the thread that owns it, which must run an event loop.
class NotifySound : public QObject {
public:
    explicit NotifySound(QString folder, QObject* parent = nullptr);
    ~NotifySound() override;

    void play();

private:
    // The recording to play, or empty when there is none to be found.
    QString recording() const;
    void playBuiltIn();

    const QString folder_;
    QByteArray pcm_;
    std::unique_ptr<QBuffer> buffer_;
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> playerOutput_;
};

}  // namespace bazarish::app
