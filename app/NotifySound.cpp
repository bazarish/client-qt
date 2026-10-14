// Bazarish project (c) 2026
#include "NotifySound.hpp"

#include <QAudioDevice>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QSoundEffect>
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
    return durationMs_;
}

void NotifySound::play()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        return;
    }
    if (!effect_) {
        effect_ = std::make_unique<QSoundEffect>();
        connect(effect_.get(), &QSoundEffect::statusChanged, this, [this]() {
            if (effect_->status() == QSoundEffect::Error) {
                bazarish::log::warn("notification sound: {} did not load",
                    effect_->source().toString().toStdString());
            }
        });
        connect(effect_.get(), &QSoundEffect::playingChanged, this, [this]() {
            if (effect_->isPlaying()) {
                playing_.start();
            } else {
                durationMs_ = playing_.elapsed();
            }
        });
    }
    const QUrl wanted = source();
    if (effect_->source() != wanted) {
        effect_->setSource(wanted);
    }
    effect_->play();
}

}  // namespace bazarish::app
