// Bazarish project (c) 2026
#include "NotifySound.hpp"

#include "AudioCodec.hpp"
#include "QtAudioIo.hpp"

#include <QAudioOutput>
#include <QAudioSink>
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace bazarish::app {

namespace {

// Noise shaped into the band where a hiss sits: below this it turns into a rumble,
// above it into a click.
constexpr double kLowPassHz = 6500.0;
constexpr double kHighPassHz = 2800.0;
constexpr int kSoundMs = 200;
// Long enough not to click, short enough to still read as a single event.
constexpr double kAttackMs = 12.0;
// Time constant of the tail, not its length: the sound is audible for about three
// of these.
constexpr double kDecayMs = 55.0;
// The finished sound is scaled to this peak. Said as a level rather than as a
// gain because the band-pass takes an unpredictable part of the noise with it,
// and what matters is how loud the result is.
constexpr double kPeakLevel = 0.15;
constexpr int kMsPerSecond = 1000;
// A fixed seed: the same noise every time, so the notification always sounds like
// itself rather than like a different hiss on every message.
constexpr std::uint32_t kNoiseSeed = 0x62617A31;  // "baz1"
// What a recording of one's own has to be called, in the order it is looked for.
const QStringList& soundNames()
{
    static const QStringList kNames{QStringLiteral("notify.wav"), QStringLiteral("notify.ogg"),
        QStringLiteral("notify.opus"), QStringLiteral("notify.flac"),
        QStringLiteral("notify.mp3")};
    return kNames;
}

// One-pole low-pass coefficient for a cutoff, at the call sample rate.
double poleFor(const double cutoffHz)
{
    return 1.0 - std::exp(-2.0 * M_PI * cutoffHz / static_cast<double>(kCallSampleRate));
}

QByteArray renderHiss()
{
    const int samples = kCallSampleRate * kSoundMs / kMsPerSecond;
    std::vector<double> shaped(static_cast<std::size_t>(samples));
    std::mt19937 noise(kNoiseSeed);
    std::uniform_real_distribution<double> white(-1.0, 1.0);
    const double lowPole = poleFor(kLowPassHz);
    const double highPole = poleFor(kHighPassHz);
    double low = 0.0;
    double high = 0.0;
    double peak = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double source = white(noise);
        low += lowPole * (source - low);
        high += highPole * (low - high);
        // The band is what the low-pass kept minus what a slower one keeps: two
        // poles are enough for a hiss and cost nothing.
        const double band = low - high;
        const double ms = static_cast<double>(i) * kMsPerSecond / kCallSampleRate;
        const double envelope = ms < kAttackMs ? ms / kAttackMs
                                               : std::exp(-(ms - kAttackMs) / kDecayMs);
        shaped[static_cast<std::size_t>(i)] = band * envelope;
        peak = std::max(peak, std::abs(shaped[static_cast<std::size_t>(i)]));
    }
    QByteArray pcm(static_cast<qsizetype>(samples) * static_cast<qsizetype>(sizeof(std::int16_t)),
        Qt::Uninitialized);
    auto* const out = reinterpret_cast<std::int16_t*>(pcm.data());
    const double scale = peak > 0.0 ? kPeakLevel / peak : 0.0;
    for (int i = 0; i < samples; ++i) {
        out[i] = static_cast<std::int16_t>(shaped[static_cast<std::size_t>(i)] * scale
            * std::numeric_limits<std::int16_t>::max());
    }
    return pcm;
}

}  // namespace

NotifySound::NotifySound(QString folder, QObject* const parent)
    : QObject(parent)
    , folder_(std::move(folder))
    , pcm_(renderHiss())
{
}

NotifySound::~NotifySound() = default;

QString NotifySound::recording() const
{
    if (folder_.isEmpty()) {
        return {};
    }
    const QDir dir(folder_);
    for (const QString& name : soundNames()) {
        const QFileInfo file(dir.filePath(name));
        if (file.isFile()) {
            return file.absoluteFilePath();
        }
    }
    return {};
}

void NotifySound::play()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        return;  // no output device: the popup still shows, which is the point
    }
    const QString own = recording();
    if (own.isEmpty()) {
        playBuiltIn();
        return;
    }
    if (!player_) {
        player_ = std::make_unique<QMediaPlayer>();
        playerOutput_ = std::make_unique<QAudioOutput>();
        player_->setAudioOutput(playerOutput_.get());
    }
    player_->setSource(QUrl::fromLocalFile(own));
    player_->play();
}

void NotifySound::playBuiltIn()
{
    if (sink_) {
        sink_->stop();
    }
    buffer_ = std::make_unique<QBuffer>();
    buffer_->setData(pcm_);
    buffer_->open(QIODevice::ReadOnly);
    sink_ = std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(), callAudioFormat());
    sink_->start(buffer_.get());
}

}  // namespace bazarish::app
