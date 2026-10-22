// Bazarish project (c) 2026
#include "VoiceNote.hpp"

#include "AudioRoute.hpp"
#include "QtAudioIo.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidCallAudio.hpp"
#endif

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QDateTime>
#include <QAudioDevice>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace bazarish::app {

namespace {

constexpr int kIdleSleepMs = 5;

constexpr int kMillisecondsPerSecond = 1000;

constexpr double kFullScale = 32768.0;

constexpr int kVoiceBitrateBps = 24000;

constexpr int kBitsPerByte = 8;
constexpr std::size_t kFrameBytesEstimate = static_cast<std::size_t>(kVoiceBitrateBps)
    * kCallFrameMs / (kBitsPerByte * 1000);

float frameLevel(const std::vector<std::int16_t>& pcm)
{
    double sum = 0.0;
    for (const std::int16_t sample : pcm) {
        const double value = static_cast<double>(sample) / kFullScale;
        sum += value * value;
    }
    return pcm.empty() ? 0.0F : static_cast<float>(std::sqrt(sum / static_cast<double>(pcm.size())));
}

}  // namespace

VoiceNote::VoiceNote(QObject* const parent)
    : QObject(parent)
{
}

VoiceNote::~VoiceNote()
{
    cancelRecording();
    stop();
}

void VoiceNote::startRecording()
{
    if (recording_.load()) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(pcmMutex_);
        pcm_.clear();
    }
    inputLevel_.store(0.0F);
    encodedBytes_.store(0);
#ifdef Q_OS_ANDROID
    // The microphone a call speaks into, with the platform's voice processing.
    source_ = std::make_unique<AndroidCallSource>();
#else
    if (AudioRoute::instance().input().isNull()) {
        throw std::runtime_error("no microphone to record from");
    }
    source_ = std::make_unique<QtAudioSource>();
#endif
    source_->start();
    startedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    recording_.store(true);
    captureThread_ = std::thread([this]() {
        while (recording_.load()) {
            const std::vector<std::int16_t> frame = source_->readFrame();
            if (frame.size() != static_cast<std::size_t>(kCallSamplesPerFrame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kIdleSleepMs));
                continue;
            }
            inputLevel_.store(frameLevel(frame));
            encodedBytes_.fetch_add(kFrameBytesEstimate);
            const std::lock_guard<std::mutex> lock(pcmMutex_);
            pcm_.insert(pcm_.end(), frame.begin(), frame.end());
        }
    });
}

void VoiceNote::stopCaptureThread()
{
    recording_.store(false);
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    if (source_) {
        source_->stop();
        source_.reset();
    }
    inputLevel_.store(0.0F);
}

Bytes VoiceNote::stopRecording()
{
    stopCaptureThread();
    std::vector<std::int16_t> pcm;
    {
        const std::lock_guard<std::mutex> lock(pcmMutex_);
        pcm.swap(pcm_);
    }
    normalizeVoicePcm(pcm);
    AudioEncoder encoder(kVoiceBitrateBps);
    std::vector<Bytes> frames;
    frames.reserve(pcm.size() / static_cast<std::size_t>(kCallSamplesPerFrame));
    for (std::size_t at = 0; at + kCallSamplesPerFrame <= pcm.size();
        at += static_cast<std::size_t>(kCallSamplesPerFrame)) {
        try {
            frames.push_back(encoder.encode(&pcm[at], kCallSamplesPerFrame));
        } catch (const std::exception& error) {
            bazarish::log::warn("a voice frame did not encode: {}", error.what());
        }
    }
    return packOpusFrames(frames);
}

void VoiceNote::cancelRecording()
{
    stopCaptureThread();
    const std::lock_guard<std::mutex> lock(pcmMutex_);
    pcm_.clear();
}

qint64 VoiceNote::elapsedMs() const
{
    return recording_.load() ? QDateTime::currentMSecsSinceEpoch() - startedAtMs_ : 0;
}

void VoiceNote::play(const Bytes& opus, const double speed, const qint64 fromMs)
{
    stop();
    const std::vector<Bytes> frames = unpackOpusFrames(opus);
    if (frames.empty()) {
        return;
    }
#ifdef Q_OS_ANDROID
    sink_ = std::make_unique<AndroidVoiceSink>();
#else
    sink_ = std::make_unique<QtAudioSink>();
#endif
    sink_->start();
    playing_.store(true);
    playedMs_.store(std::max<qint64>(0, fromMs));
    playbackThread_ = std::thread([this, frames, speed, fromMs]() {
        std::vector<std::int16_t> pcm;
        pcm.reserve(frames.size() * static_cast<std::size_t>(kCallSamplesPerFrame));
        AudioDecoder decoder;
        for (const Bytes& frame : frames) {
            try {
                const std::vector<std::int16_t> decoded = decoder.decode(frame);
                pcm.insert(pcm.end(), decoded.begin(), decoded.end());
            } catch (const std::exception& error) {
                bazarish::log::warn("a voice frame did not decode: {}", error.what());
                break;
            }
        }
        const std::size_t from = static_cast<std::size_t>(
            std::max<qint64>(0, fromMs) * kCallSampleRate / kMillisecondsPerSecond);
        if (from >= pcm.size()) {
            pcm.clear();
        } else if (from > 0) {
            pcm.erase(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(from));
        }
        TimeStretch stretch(std::move(pcm), speed);
        std::vector<std::int16_t> out(static_cast<std::size_t>(kCallSamplesPerFrame));
        std::size_t playedSamples = 0;
        bool ended = false;
        while (playing_.load()) {
            const std::size_t produced = stretch.read(out.data(), out.size());
            if (produced == 0) {
                ended = true;
                break;
            }
            out.resize(produced);
            sink_->writeFrame(out);
            out.resize(static_cast<std::size_t>(kCallSamplesPerFrame));
            playedSamples += produced;
            playedMs_.store(std::max<qint64>(0, fromMs)
                + static_cast<qint64>(static_cast<double>(playedSamples) * speed
                    * kMillisecondsPerSecond / kCallSampleRate));
            std::this_thread::sleep_for(std::chrono::milliseconds(kCallFrameMs));
        }
        playing_.store(false);
        if (!ended) {
            return;
        }
        // The output, and with it the ear sensor, closes on the thread that opened it.
        QMetaObject::invokeMethod(this, [this]() {
            if (!sink_ || playing_.load()) {
                return;
            }
            stopPlaybackThread();
            emit playbackFinished();
        }, Qt::QueuedConnection);
    });
}

void VoiceNote::stopPlaybackThread()
{
    playing_.store(false);
    if (playbackThread_.joinable()) {
        playbackThread_.join();
    }
    if (sink_) {
        sink_->stop();
        sink_.reset();
    }
}

void VoiceNote::stop()
{
    stopPlaybackThread();
}

}  // namespace bazarish::app
