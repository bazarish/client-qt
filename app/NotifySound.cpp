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

const char* const kBuiltInPrefix = "qrc:/sound/";

}  // namespace

NotifySound::NotifySound(QString folder, QString fileName, QObject* const parent)
    : QObject(parent)
    , folder_(std::move(folder))
    , fileName_(std::move(fileName))
{
}

NotifySound::~NotifySound() = default;

QUrl NotifySound::source() const
{
    if (!folder_.isEmpty()) {
        const QFileInfo file(QDir(folder_).filePath(fileName_));
        if (file.isFile()) {
            return QUrl::fromLocalFile(file.absoluteFilePath());
        }
    }
    return QUrl(QString::fromLatin1(kBuiltInPrefix) + fileName_);
}

qint64 NotifySound::durationMs() const
{
    return player_ ? player_->duration() : 0;
}

void NotifySound::play()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        return;
    }
    if (!player_) {
        player_ = std::make_unique<QMediaPlayer>();
        output_ = std::make_unique<QAudioOutput>();
        player_->setAudioOutput(output_.get());
        connect(player_.get(), &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& text) {
                bazarish::log::warn("notification sound: {} ({})", text.toStdString(),
                    player_->source().toString().toStdString());
            });
    }
    const QUrl wanted = source();
    if (player_->source() != wanted) {
        player_->setSource(wanted);
    }
    player_->stop();
    player_->play();
}

}  // namespace bazarish::app
