// Bazarish project (c) 2026
#include "Ringtone.hpp"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

namespace bazarish::app {

namespace {

const char* const kRingtone = "qrc:/sound/ringtone.wav";

}  // namespace

Ringtone::Ringtone(QObject* const parent)
    : QObject(parent)
{
}

Ringtone::~Ringtone() = default;

void Ringtone::start()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        return;  // no output device: the call window is still there to be answered
    }
    if (!player_) {
        player_ = std::make_unique<QMediaPlayer>();
        output_ = std::make_unique<QAudioOutput>();
        player_->setAudioOutput(output_.get());
        player_->setSource(QUrl(QString::fromLatin1(kRingtone)));
        player_->setLoops(QMediaPlayer::Infinite);
        // A ringtone that will not play is a call that arrives in silence, so it
        // is said out loud here rather than left to be noticed as a missed call.
        connect(player_.get(), &QMediaPlayer::errorOccurred, this,
            [](QMediaPlayer::Error, const QString& text) {
                bazarish::log::warn("ringtone: {}", text.toStdString());
            });
    }
    if (player_->playbackState() == QMediaPlayer::PlayingState) {
        return;
    }
    player_->play();
}

void Ringtone::stop()
{
    if (player_) {
        player_->stop();
    }
}

}  // namespace bazarish::app
