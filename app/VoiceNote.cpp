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

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace bazarish::app {

namespace {

// One frame of the call format is 20 ms; a recorder that reads slower than that
// falls behind, so it sleeps only when the source has nothing yet.
constexpr int kIdleSleepMs = 5;

// Full scale of a signed-16 sample: the reference the input level is measured
// against.
constexpr double kFullScale = 32768.0;

// What a voice note may spend on a second of speech. A call lets the codec
// decide and adapt; a recording is bounded by the message it has to fit in, so
// it is told. Opus at this rate is speech quality at 48 kHz mono.
constexpr int kVoiceBitrateBps = 24000;

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

void VoiceNote::startRecording()
{
    if (recording_.load()) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(framesMutex_);
        frames_.clear();
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
        AudioEncoder encoder(kVoiceBitrateBps);
        while (recording_.load()) {
            const std::vector<std::int16_t> pcm = source_->readFrame();
            if (pcm.size() != static_cast<std::size_t>(kCallSamplesPerFrame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kIdleSleepMs));
                continue;
            }
            inputLevel_.store(frameLevel(pcm));
            try {
                Bytes packet = encoder.encode(pcm.data(), kCallSamplesPerFrame);
                encodedBytes_.fetch_add(packet.size());
                const std::lock_guard<std::mutex> lock(framesMutex_);
                frames_.push_back(std::move(packet));
            } catch (const std::exception& error) {
                bazarish::log::warn("a voice frame did not encode: {}", error.what());
            }
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
    const std::lock_guard<std::mutex> lock(framesMutex_);
    const Bytes packed = packOpusFrames(frames_);
    frames_.clear();
    return packed;
}

void VoiceNote::cancelRecording()
{
    stopCaptureThread();
    const std::lock_guard<std::mutex> lock(framesMutex_);
    frames_.clear();
}

qint64 VoiceNote::elapsedMs() const
{
    return recording_.load() ? QDateTime::currentMSecsSinceEpoch() - startedAtMs_ : 0;
}

void VoiceNote::play(const Bytes& opus, const double speed)
{
    stop();
    const std::vector<Bytes> frames = unpackOpusFrames(opus);
    if (frames.empty()) {
        return;
    }
    sink_ = std::make_unique<QtAudioSink>();
    sink_->start();
    playing_.store(true);
    playbackThread_ = std::thread([this, frames, speed]() {
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
        TimeStretch stretch(std::move(pcm), speed);
        std::vector<std::int16_t> out(static_cast<std::size_t>(kCallSamplesPerFrame));
        while (playing_.load()) {
            const std::size_t produced = stretch.read(out.data(), out.size());
            if (produced == 0) {
                break;
            }
            out.resize(produced);
            sink_->writeFrame(out);
            out.resize(static_cast<std::size_t>(kCallSamplesPerFrame));
            std::this_thread::sleep_for(std::chrono::milliseconds(kCallFrameMs));
        }
        playing_.store(false);
        emit playbackFinished();
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
