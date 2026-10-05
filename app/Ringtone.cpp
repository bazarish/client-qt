// Bazarish project (c) 2026
#include "Ringtone.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <QtEndian>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace bazarish::app {

namespace {

const char* const kBuiltInTrack = ":/sound/ringtone.wav";
const char* const kTrackName = "ringtone.wav";

constexpr qint64 kMaxTrackBytes = 8 * 1024 * 1024;

constexpr int kEnvelopeFrameMs = 20;
constexpr int kLevelIntervalMs = 33;
constexpr int kMsPerSecond = 1000;
constexpr int kUsPerMs = 1000;
constexpr qint64 kUsPerSecond = static_cast<qint64>(kUsPerMs) * kMsPerSecond;

constexpr std::uint16_t kPcmFormatTag = 1;
constexpr int kBitsPerSample = 16;
constexpr int kChannels = 1;

constexpr int kRiffHeaderBytes = 12;
constexpr int kChunkHeaderBytes = 8;
constexpr int kTagBytes = 4;
constexpr int kWaveTagOffset = 8;
constexpr int kChunkSizeOffset = 4;
constexpr int kFormatTagOffset = 0;
constexpr int kChannelsOffset = 2;
constexpr int kSampleRateOffset = 4;
constexpr int kBitsPerSampleOffset = 14;
constexpr int kMinFormatChunkBytes = 16;

std::uint16_t readU16(const char* const at)
{
    std::uint16_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return qFromLittleEndian(value);
}

std::uint32_t readU32(const char* const at)
{
    std::uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return qFromLittleEndian(value);
}

bool parseWav(const QByteArray& bytes, int& sampleRate, std::vector<std::int16_t>& samples)
{
    if (bytes.size() < kRiffHeaderBytes
        || std::memcmp(bytes.constData(), "RIFF", kTagBytes) != 0
        || std::memcmp(bytes.constData() + kWaveTagOffset, "WAVE", kTagBytes) != 0) {
        return false;
    }
    bool haveFormat = false;
    qsizetype at = kRiffHeaderBytes;
    while (at + kChunkHeaderBytes <= bytes.size()) {
        const char* const header = bytes.constData() + at;
        const qsizetype size = static_cast<qsizetype>(readU32(header + kChunkSizeOffset));
        const qsizetype body = at + kChunkHeaderBytes;
        if (body + size > bytes.size()) {
            return false;
        }
        if (std::memcmp(header, "fmt ", kTagBytes) == 0) {
            if (size < kMinFormatChunkBytes) {
                return false;
            }
            const char* const format = bytes.constData() + body;
            if (readU16(format + kFormatTagOffset) != kPcmFormatTag
                || readU16(format + kChannelsOffset) != kChannels
                || readU16(format + kBitsPerSampleOffset) != kBitsPerSample) {
                return false;
            }
            sampleRate = static_cast<int>(readU32(format + kSampleRateOffset));
            haveFormat = sampleRate > 0;
        } else if (std::memcmp(header, "data", kTagBytes) == 0) {
            if (!haveFormat) {
                return false;
            }
            const qsizetype count = size / static_cast<qsizetype>(sizeof(std::int16_t));
            samples.resize(static_cast<std::size_t>(count));
            std::memcpy(samples.data(), bytes.constData() + body,
                static_cast<std::size_t>(count) * sizeof(std::int16_t));
            return count > 0;
        }
        at = body + size + (size % 2);
    }
    return false;
}

}  // namespace

class Ringtone::Loop : public QIODevice {
public:
    explicit Loop(const std::vector<std::int16_t>& samples)
        : samples_(samples)
    {
    }

protected:
    qint64 readData(char* const data, const qint64 maxLen) override
    {
        if (samples_.empty()) {
            return 0;
        }
        const qint64 want = maxLen / static_cast<qint64>(sizeof(std::int16_t));
        auto* const out = reinterpret_cast<std::int16_t*>(data);
        for (qint64 i = 0; i < want; ++i) {
            out[i] = samples_[position_];
            position_ = (position_ + 1) % samples_.size();
        }
        return want * static_cast<qint64>(sizeof(std::int16_t));
    }

    qint64 writeData(const char*, qint64) override
    {
        return 0;
    }

    bool isSequential() const override
    {
        return true;
    }

private:
    const std::vector<std::int16_t>& samples_;
    std::size_t position_ = 0;
};

Ringtone::Ringtone(QString folder, QObject* const parent)
    : QObject(parent)
    , folder_(std::move(folder))
{
}

Ringtone::~Ringtone() = default;

bool Ringtone::loadFrom(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        bazarish::log::warn("ringtone: {} could not be read", path.toStdString());
        return false;
    }
    if (file.size() > kMaxTrackBytes) {
        bazarish::log::warn("ringtone: {} is larger than {} bytes", path.toStdString(),
            kMaxTrackBytes);
        return false;
    }
    if (!parseWav(file.readAll(), sampleRate_, samples_)) {
        bazarish::log::warn("ringtone: {} is not 16-bit mono PCM", path.toStdString());
        samples_.clear();
        return false;
    }
    frameSamples_ = static_cast<std::size_t>(sampleRate_) * kEnvelopeFrameMs / kMsPerSecond;
    if (frameSamples_ == 0) {
        bazarish::log::warn(
            "ringtone: {} is sampled at {} Hz, too low to measure", path.toStdString(),
            sampleRate_);
        samples_.clear();
        return false;
    }
    envelope_.clear();
    float loudest = 0.0F;
    for (std::size_t at = 0; at < samples_.size(); at += frameSamples_) {
        const std::size_t end = std::min(at + frameSamples_, samples_.size());
        int peak = 0;
        for (std::size_t i = at; i < end; ++i) {
            peak = std::max(peak, std::abs(static_cast<int>(samples_[i])));
        }
        envelope_.push_back(static_cast<float>(peak));
        loudest = std::max(loudest, envelope_.back());
    }
    if (envelope_.empty() || loudest <= 0.0F) {
        bazarish::log::warn("ringtone: {} is silent", path.toStdString());
        samples_.clear();
        return false;
    }
    for (float& value : envelope_) {
        value /= loudest;
    }
    return true;
}

bool Ringtone::loadTrack()
{
    if (!folder_.isEmpty()) {
        const QString own = QDir(folder_).filePath(QString::fromLatin1(kTrackName));
        if (QFileInfo(own).isFile() && loadFrom(own)) {
            return true;
        }
    }
    return loadFrom(QString::fromLatin1(kBuiltInTrack));
}

void Ringtone::start()
{
    if (sink_) {
        return;
    }
    if (!loadTrack()) {
        return;
    }
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        return;
    }
    QAudioFormat format;
    format.setSampleRate(sampleRate_);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        bazarish::log::warn(
            "ringtone: the audio device does not take {} Hz mono 16-bit", sampleRate_);
        return;
    }
    loop_ = std::make_unique<Loop>(samples_);
    if (!loop_->open(QIODevice::ReadOnly)) {
        bazarish::log::warn("ringtone: the track could not be opened for playing");
        loop_.reset();
        return;
    }
    sink_ = std::make_unique<QAudioSink>(device, format);
    sink_->start(loop_.get());
    if (!levelTimer_) {
        levelTimer_ = std::make_unique<QTimer>();
        levelTimer_->setInterval(kLevelIntervalMs);
        connect(levelTimer_.get(), &QTimer::timeout, this, &Ringtone::publishLevel);
    }
    levelTimer_->start();
}

void Ringtone::stop()
{
    if (levelTimer_) {
        levelTimer_->stop();
    }
    if (sink_) {
        sink_->stop();
        sink_.reset();
    }
    loop_.reset();
    emit levelChanged(0.0);
}

void Ringtone::publishLevel()
{
    if (!sink_ || envelope_.empty()) {
        return;
    }
    const qint64 played = sink_->processedUSecs() * sampleRate_ / kUsPerSecond;
    const std::size_t at = static_cast<std::size_t>(played) % samples_.size();
    emit levelChanged(static_cast<qreal>(envelope_[at / frameSamples_]));
}

}  // namespace bazarish::app
