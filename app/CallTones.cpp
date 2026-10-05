// Bazarish project (c) 2026
#include "CallTones.hpp"

#include "AudioCodec.hpp"
#include "QtAudioIo.hpp"

#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace bazarish::app {

namespace {

constexpr double kToneHz = 425.0;
constexpr double kAmplitude = 0.22;
constexpr double kRampMs = 8.0;
constexpr int kMsPerSecond = 1000;

constexpr int kRingbackOnMs = 1000;
constexpr int kRingbackOffMs = 4000;
constexpr int kBusyOnMs = 250;
constexpr int kBusyOffMs = 250;
constexpr int kBusyBursts = 4;
constexpr int kForever = 0;

}  // namespace

class CallTones::Voice : public QIODevice {
public:
    Voice(const int onMs, const int offMs, const int bursts)
        : onMs_(onMs)
        , offMs_(offMs)
        , bursts_(bursts)
    {
    }

    int durationMs() const
    {
        return bursts_ * (onMs_ + offMs_);
    }

protected:
    qint64 readData(char* data, const qint64 maxLen) override
    {
        const std::size_t want = static_cast<std::size_t>(maxLen) / sizeof(std::int16_t);
        auto* const out = reinterpret_cast<std::int16_t*>(data);
        for (std::size_t i = 0; i < want; ++i) {
            out[i] = sampleAt(position_++);
        }
        return static_cast<qint64>(want * sizeof(std::int16_t));
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
    std::int16_t sampleAt(const qint64 position) const
    {
        const qint64 cycleMs = onMs_ + offMs_;
        const qint64 elapsedMs = position * kMsPerSecond / kCallSampleRate;
        if (bursts_ != kForever && elapsedMs >= bursts_ * cycleMs) {
            return 0;
        }
        const qint64 phaseMs = elapsedMs % cycleMs;
        if (phaseMs >= onMs_) {
            return 0;
        }
        const double ramp = std::min({1.0, static_cast<double>(phaseMs) / kRampMs,
            static_cast<double>(onMs_ - phaseMs) / kRampMs});
        const double angle = 2.0 * std::numbers::pi * kToneHz
            * static_cast<double>(position) / static_cast<double>(kCallSampleRate);
        const double value = std::sin(angle) * kAmplitude * ramp;
        return static_cast<std::int16_t>(value * std::numeric_limits<std::int16_t>::max());
    }

    const int onMs_;
    const int offMs_;
    const int bursts_;
    qint64 position_ = 0;
};

CallTones::CallTones(QObject* const parent)
    : QObject(parent)
{
}

CallTones::~CallTones()
{
    stop();
}

void CallTones::ringback()
{
    play(kRingbackOnMs, kRingbackOffMs, kForever);
}

void CallTones::failure()
{
    play(kBusyOnMs, kBusyOffMs, kBusyBursts);
}

void CallTones::endRingback()
{
    if (voice_ && voice_->durationMs() == 0) {
        stop();
    }
}

void CallTones::play(const int onMs, const int offMs, const int bursts)
{
    if (voice_ && voice_->durationMs() == 0 && bursts == kForever && sink_) {
        return;
    }
    stop();
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        return;
    }
    voice_ = std::make_unique<Voice>(onMs, offMs, bursts);
    voice_->open(QIODevice::ReadOnly);
    sink_ = std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(), callAudioFormat());
    sink_->start(voice_.get());
    if (bursts != kForever) {
        QTimer::singleShot(voice_->durationMs(), this, [this]() { stop(); });
    }
}

void CallTones::stop()
{
    if (sink_) {
        sink_->stop();
        sink_.reset();
    }
    voice_.reset();
}

}  // namespace bazarish::app
