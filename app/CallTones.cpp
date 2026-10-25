// Bazarish project (c) 2026
#include "CallTones.hpp"

#include "AudioCodec.hpp"
#include "AudioRoute.hpp"
#include "QtAudioIo.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidCallAudio.hpp"
#endif

#include <QAudioSink>
#include <QIODevice>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace bazarish::app {

namespace {

constexpr double kToneHz = 425.0;
constexpr double kHangupHz = 300.0;
constexpr int kHangupMs = 400;
constexpr int kOnce = 1;
constexpr double kAmplitude = 0.22;
constexpr double kRampMs = 8.0;
constexpr int kMsPerSecond = 1000;

constexpr int kRingbackOnMs = 1000;
constexpr int kRingbackOffMs = 4000;
constexpr int kBusyOnMs = 250;
constexpr int kBusyOffMs = 250;
constexpr int kBusyBursts = 4;
constexpr int kForever = 0;
constexpr int kPrefilledFrames = 2;

}  // namespace

class CallTones::Voice : public QIODevice {
public:
    Voice(const double hz, const int onMs, const int offMs, const int bursts)
        : hz_(hz)
        , onMs_(onMs)
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
        const double angle = 2.0 * std::numbers::pi * hz_
            * static_cast<double>(position) / static_cast<double>(kCallSampleRate);
        const double value = std::sin(angle) * kAmplitude * ramp;
        return static_cast<std::int16_t>(value * std::numeric_limits<std::int16_t>::max());
    }

    const double hz_;
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
    play(kToneHz, kRingbackOnMs, kRingbackOffMs, kForever);
}

void CallTones::failure()
{
    play(kToneHz, kBusyOnMs, kBusyOffMs, kBusyBursts);
}

void CallTones::hangup()
{
    play(kHangupHz, kHangupMs, 0, kOnce);
}

void CallTones::endRingback()
{
    if (voice_ && voice_->durationMs() == 0) {
        stop();
    }
}

void CallTones::play(const double hz, const int onMs, const int offMs, const int bursts)
{
    if (voice_ && voice_->durationMs() == 0 && bursts == kForever) {
        return;
    }
    stop();
#ifdef Q_OS_ANDROID
    // A phone sounds the tones where the call will speak: the earpiece, or the speaker if chosen.
    voice_ = std::make_unique<Voice>(hz, onMs, offMs, bursts);
    voice_->open(QIODevice::ReadOnly);
    phone_ = std::make_unique<AndroidCallSink>();
    phone_->start();
    feeder_ = std::make_unique<QTimer>();
    feeder_->setInterval(kCallFrameMs);
    connect(feeder_.get(), &QTimer::timeout, this, &CallTones::feed);
    for (int i = 0; i < kPrefilledFrames; ++i) {
        feed();
    }
    feeder_->start();
#else
    const QAudioDevice output = AudioRoute::instance().output();
    if (output.isNull()) {
        return;
    }
    voice_ = std::make_unique<Voice>(hz, onMs, offMs, bursts);
    voice_->open(QIODevice::ReadOnly);
    sink_ = std::make_unique<QAudioSink>(output, callAudioFormat());
    sink_->start(voice_.get());
#endif
    if (bursts != kForever) {
        QTimer::singleShot(voice_->durationMs(), this, [this]() { stop(); });
    }
}

void CallTones::feed()
{
#ifdef Q_OS_ANDROID
    std::vector<std::int16_t> frame(static_cast<std::size_t>(kCallSamplesPerFrame));
    const qint64 bytes = static_cast<qint64>(frame.size() * sizeof(std::int16_t));
    if (voice_->read(reinterpret_cast<char*>(frame.data()), bytes) != bytes) {
        throw std::runtime_error("a call tone came up short of a frame");
    }
    phone_->writeFrame(frame);
#endif
}

void CallTones::stop()
{
#ifdef Q_OS_ANDROID
    feeder_.reset();
    if (phone_) {
        phone_->stop();
        phone_.reset();
    }
#endif
    if (sink_) {
        sink_->stop();
        sink_.reset();
    }
    voice_.reset();
}

}  // namespace bazarish::app
