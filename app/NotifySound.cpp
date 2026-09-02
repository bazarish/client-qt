// Bazarish project (c) 2026
#include "NotifySound.hpp"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

namespace bazarish::app {

namespace {

// The sound carried inside the application, used unless the user leaves one of
// their own at the root of the installation, under this name. One name and one
// format, the same rule the ringtone follows: a known file, not a search.
const char* const kBuiltInSound = "qrc:/sound/notify.wav";
const char* const kSoundName = "notify.wav";

}  // namespace

NotifySound::NotifySound(QString folder, QObject* const parent)
    : QObject(parent)
    , folder_(std::move(folder))
{
}

NotifySound::~NotifySound() = default;

QUrl NotifySound::source() const
{
    if (!folder_.isEmpty()) {
        const QFileInfo file(QDir(folder_).filePath(QString::fromLatin1(kSoundName)));
        if (file.isFile()) {
            return QUrl::fromLocalFile(file.absoluteFilePath());
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
        // A recording of the user's own that will not play is otherwise a silence
        // with nothing to explain it.
        connect(player_.get(), &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& text) {
                bazarish::log::warn("notification sound: {} ({})", text.toStdString(),
                    player_->source().toString().toStdString());
            });
    }
    // Read every time: a file put at the root of the installation is picked up
    // without restarting, and taken away again the same way.
    const QUrl wanted = source();
    if (player_->source() != wanted) {
        player_->setSource(wanted);
    }
    player_->stop();  // a second arrival restarts the sound rather than being lost
    player_->play();
}

}  // namespace bazarish::app
