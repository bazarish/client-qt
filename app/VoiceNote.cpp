// Bazarish project (c) 2026
#include "VoiceNote.hpp"

#include "QtAudioIo.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QDateTime>

#include <chrono>
#include <stdexcept>

namespace bazarish::app {

namespace {

// One frame of the call format is 20 ms; a recorder that reads slower than that
// falls behind, so it sleeps only when the source has nothing yet.
constexpr int kIdleSleepMs = 5;

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
    source_ = std::make_unique<QtAudioSource>();
    source_->start();
    startedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    recording_.store(true);
    captureThread_ = std::thread([this]() {
        AudioEncoder encoder;
        while (recording_.load()) {
            const std::vector<std::int16_t> pcm = source_->readFrame();
            if (pcm.size() != static_cast<std::size_t>(kCallSamplesPerFrame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kIdleSleepMs));
                continue;
            }
            try {
                Bytes packet = encoder.encode(pcm.data(), kCallSamplesPerFrame);
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

void VoiceNote::play(const Bytes& opus)
{
    stop();
    const std::vector<Bytes> frames = unpackOpusFrames(opus);
    if (frames.empty()) {
        return;
    }
    sink_ = std::make_unique<QtAudioSink>();
    sink_->start();
    playing_.store(true);
    playbackThread_ = std::thread([this, frames]() {
        AudioDecoder decoder;
        for (const Bytes& frame : frames) {
            if (!playing_.load()) {
                break;
            }
            try {
                sink_->writeFrame(decoder.decode(frame));
            } catch (const std::exception& error) {
                bazarish::log::warn("a voice frame did not decode: {}", error.what());
                break;
            }
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
