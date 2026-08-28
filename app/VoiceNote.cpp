// Bazarish project (c) 2026
#include "VoiceNote.hpp"

#include "QtAudioIo.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QDateTime>
#include <QAudioDevice>
#include <QMediaDevices>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace bazarish::app {

namespace {

// One frame of the call format is 20 ms; a recorder that reads slower than that
// falls behind, so it sleeps only when the source has nothing yet.
constexpr int kIdleSleepMs = 5;

// Sample counts are per second; positions are reported in milliseconds.
constexpr int kMillisecondsPerSecond = 1000;

// Full scale of a signed-16 sample: the reference the input level is measured
// against.
constexpr double kFullScale = 32768.0;

// What a voice note may spend on a second of speech. A call lets the codec
// decide and adapt; a recording is bounded by the message it has to fit in, so
// it is told. Opus at this rate is speech quality at 48 kHz mono.
constexpr int kVoiceBitrateBps = 24000;

// What one frame will weigh once encoded, at the bitrate above. Nothing is
// encoded until the recording ends, so the size the UI stops at is this.
constexpr int kBitsPerByte = 8;
constexpr std::size_t kFrameBytesEstimate = static_cast<std::size_t>(kVoiceBitrateBps)
    * kCallFrameMs / (kBitsPerByte * 1000);

// Loudness of one captured frame, 0..1.
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

void VoiceNote::startMonitoring()
{
    if (recording_.load()) {
        return;
    }
    monitorOnly_.store(true);
    startRecording();
}

void VoiceNote::stopMonitoring()
{
    if (!monitorOnly_.load()) {
        return;  // a real take is running; it is not this to stop
    }
    stopCaptureThread();
    monitorOnly_.store(false);
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
    // Named before the fact: with no input device the recorder would run,
    // draw a flat line and end with nothing to send, which says the same thing
    // far later and far less clearly.
    if (QMediaDevices::defaultAudioInput().isNull()) {
        throw std::runtime_error("no microphone to record from");
    }
    inputLevel_.store(0.0F);
    encodedBytes_.store(0);
    source_ = std::make_unique<QtAudioSource>();
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
            if (monitorOnly_.load()) {
                continue;  // shown, not kept
            }
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
    // One loudness for every message, whatever the microphone was set to.
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
    sink_ = std::make_unique<QtAudioSink>();
    sink_->start();
    playing_.store(true);
    playedMs_.store(std::max<qint64>(0, fromMs));
    playbackThread_ = std::thread([this, frames, speed, fromMs]() {
        // Decoded whole first: the stretcher needs to look ahead of what it is
        // playing, and a voice message is short enough to hold at once.
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
        // Playing from somewhere other than the start is dropping what came
        // before it: the stretcher works forward through what it is given.
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
        // Ran out on its own, rather than being stopped: only the first is the
        // end of a message. Reporting a requested stop as "finished" made a seek
        // - which stops and starts again - look like playback ending, and the
        // bubble emptied itself a moment after the press.
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
            // Reported in recording time: a message played at 2x still says where
            // in itself it has got to.
            playedSamples += produced;
            playedMs_.store(std::max<qint64>(0, fromMs)
                + static_cast<qint64>(static_cast<double>(playedSamples) * speed
                    * kMillisecondsPerSecond / kCallSampleRate));
            std::this_thread::sleep_for(std::chrono::milliseconds(kCallFrameMs));
        }
        playing_.store(false);
        if (ended) {
            emit playbackFinished();
        }
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
