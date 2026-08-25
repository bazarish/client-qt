// Bazarish project (c) 2026
#include "QtAudioIo.hpp"

#include "AudioCodec.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
#include <QThread>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <mutex>

namespace bazarish::app {

QAudioFormat callAudioFormat()
{
    QAudioFormat format;
    format.setSampleRate(kCallSampleRate);
    format.setChannelCount(kCallChannels);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

// --- capture ---

// A QIODevice that QAudioSource writes captured PCM into; the call engine pops
// fixed-size frames. Only the ring + its lock are shared across threads.
class QtAudioSource::CaptureDevice : public QIODevice {
public:
    qint64 writeData(const char* data, qint64 len) override
    {
        const auto* samples = reinterpret_cast<const std::int16_t*>(data);
        const std::size_t count = static_cast<std::size_t>(len) / sizeof(std::int16_t);
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ring_.insert(ring_.end(), samples, samples + count);
            // Bound latency: drop the oldest audio if the reader falls far behind.
            const std::size_t cap = static_cast<std::size_t>(kCallSamplesPerFrame) * 25;
            while (ring_.size() > cap) {
                ring_.pop_front();
            }
        }
        cv_.notify_one();
        return len;
    }

    qint64 readData(char*, qint64) override
    {
        return 0;  // capture device is write-only from the audio backend
    }

    bool isSequential() const override
    {
        return true;
    }

    void setRunning(const bool running)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            running_ = running;
        }
        cv_.notify_all();
    }

    std::vector<std::int16_t> popFrame()
    {
        constexpr std::size_t frame = static_cast<std::size_t>(kCallSamplesPerFrame);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100),
            [this] { return ring_.size() >= frame || !running_; });
        if (ring_.size() < frame) {
            return {};
        }
        std::vector<std::int16_t> out(ring_.begin(), ring_.begin() + frame);
        ring_.erase(ring_.begin(), ring_.begin() + frame);
        return out;
    }

private:
    std::deque<std::int16_t> ring_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = true;
};

QtAudioSource::QtAudioSource()
    : source_(std::make_unique<QAudioSource>(QMediaDevices::defaultAudioInput(), callAudioFormat()))
    , device_(std::make_unique<CaptureDevice>())
{
    device_->open(QIODevice::WriteOnly);
}

QtAudioSource::~QtAudioSource()
{
    stop();
}

namespace {

// The QAudio objects belong to the thread that created them - the account
// worker, which runs an event loop - and they drive themselves with timers that
// only tick there. The call engine starts and stops them from its own plain
// threads, so the calls are handed over instead of made directly; without that
// the device opens, no timer ever fires, and the call is silent both ways with
// nothing reported.
template <typename Fn>
void onOwnerThread(QObject* const owner, Fn&& body)
{
    if (owner->thread() == QThread::currentThread()) {
        body();
        return;
    }
    QMetaObject::invokeMethod(owner, std::forward<Fn>(body), Qt::BlockingQueuedConnection);
}

}  // namespace

void QtAudioSource::start()
{
    // A microphone that did not open is the difference between a quiet call and a
    // dead one, and it used to be reported by nothing at all.
    std::string failure;
    onOwnerThread(source_.get(), [this, &failure]() {
        device_->setRunning(true);
        source_->start(device_.get());
        const QAudioDevice input = QMediaDevices::defaultAudioInput();
        if (input.isNull()) {
            failure = "no microphone: this system offers no audio input";
            return;
        }
        if (source_->error() != QAudio::NoError) {
            failure = "the microphone did not start (" + input.description().toStdString()
                + "): audio error " + std::to_string(static_cast<int>(source_->error()));
            return;
        }
        bazarish::log::info("capture started on {}", input.description().toStdString());
    });
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
}

void QtAudioSource::stop()
{
    device_->setRunning(false);  // unblock a reader waiting on popFrame
    onOwnerThread(source_.get(), [this]() { source_->stop(); });
}

std::vector<std::int16_t> QtAudioSource::readFrame()
{
    return device_->popFrame();
}

// --- playback ---

// A QIODevice QAudioSink pulls PCM from; the call engine pushes decoded frames.
class QtAudioSink::PlaybackDevice : public QIODevice {
public:
    qint64 writeData(const char*, qint64) override
    {
        return 0;  // playback device is read-only to the audio backend
    }

    qint64 readData(char* data, qint64 maxLen) override
    {
        const std::size_t want = static_cast<std::size_t>(maxLen) / sizeof(std::int16_t);
        auto* out = reinterpret_cast<std::int16_t*>(data);
        std::size_t produced = 0;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            while (produced < want && !ring_.empty()) {
                out[produced++] = ring_.front();
                ring_.pop_front();
            }
        }
        // Fill any shortfall with silence so the sink never stalls on underrun.
        while (produced < want) {
            out[produced++] = 0;
        }
        return static_cast<qint64>(produced * sizeof(std::int16_t));
    }

    bool isSequential() const override
    {
        return true;
    }

    void push(const std::vector<std::int16_t>& pcm)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ring_.insert(ring_.end(), pcm.begin(), pcm.end());
        const std::size_t cap = static_cast<std::size_t>(kCallSamplesPerFrame) * 25;
        while (ring_.size() > cap) {
            ring_.pop_front();
        }
    }

private:
    std::deque<std::int16_t> ring_;
    std::mutex mutex_;
};

QtAudioSink::QtAudioSink()
    : sink_(std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(), callAudioFormat()))
    , device_(std::make_unique<PlaybackDevice>())
{
    device_->open(QIODevice::ReadOnly);
}

QtAudioSink::~QtAudioSink()
{
    stop();
}

void QtAudioSink::start()
{
    std::string failure;
    onOwnerThread(sink_.get(), [this, &failure]() {
        sink_->start(device_.get());
        const QAudioDevice output = QMediaDevices::defaultAudioOutput();
        if (output.isNull()) {
            failure = "no speaker: this system offers no audio output";
            return;
        }
        if (sink_->error() != QAudio::NoError) {
            failure = "the speaker did not start (" + output.description().toStdString()
                + "): audio error " + std::to_string(static_cast<int>(sink_->error()));
            return;
        }
        bazarish::log::info("playback started on {}", output.description().toStdString());
    });
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
}

void QtAudioSink::stop()
{
    onOwnerThread(sink_.get(), [this]() { sink_->stop(); });
}

void QtAudioSink::writeFrame(const std::vector<std::int16_t>& pcm)
{
    device_->push(pcm);
}

}  // namespace bazarish::app
