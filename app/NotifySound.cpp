// Bazarish project (c) 2026
#include "NotifySound.hpp"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>

namespace bazarish::app {

namespace {

// The sound carried inside the application, used unless the user leaves one of
// their own beside their accounts.
const char* const kBuiltInSound = "qrc:/sound/notify.wav";

// What a recording of one's own has to be called, in the order it is looked for.
const QStringList& soundNames()
{
    static const QStringList kNames{QStringLiteral("notify.wav"), QStringLiteral("notify.ogg"),
        QStringLiteral("notify.opus"), QStringLiteral("notify.flac"),
        QStringLiteral("notify.mp3")};
    return kNames;
}

}  // namespace

NotifySound::NotifySound(QString folder, QObject* const parent)
    : QObject(parent)
    , folder_(std::move(folder))
{
}

NotifySound::~NotifySound() = default;

QUrl NotifySound::source() const
{
    const QDir dir(folder_);
    if (!folder_.isEmpty()) {
        for (const QString& name : soundNames()) {
            const QFileInfo file(dir.filePath(name));
            if (file.isFile()) {
                return QUrl::fromLocalFile(file.absoluteFilePath());
            }
        }
    }
    return QUrl(QString::fromLatin1(kBuiltInSound));
}

void NotifySound::play()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        return;  // no output device: the popup still shows, which is the point
    }
    if (!player_) {
        player_ = std::make_unique<QMediaPlayer>();
        output_ = std::make_unique<QAudioOutput>();
        player_->setAudioOutput(output_.get());
    }
    // Read every time: a file dropped in beside the accounts is picked up without
    // restarting, and taken away again the same way.
    const QUrl wanted = source();
    if (player_->source() != wanted) {
        player_->setSource(wanted);
    }
    player_->stop();  // a second arrival restarts the sound rather than being lost
    player_->play();
}

}  // namespace bazarish::app
