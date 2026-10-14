// Bazarish project (c) 2026
#include "QtAudioIo.hpp"

#include "AudioCodec.hpp"
#include "AudioRoute.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
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

class QtAudioSource::CaptureDevice : public QIODevice {
public:
    qint64 writeData(const char* data, qint64 len) override
    {
        const auto* samples = reinterpret_cast<const std::int16_t*>(data);
        const std::size_t count = static_cast<std::size_t>(len) / sizeof(std::int16_t);
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ring_.insert(ring_.end(), samples, samples + count);
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
        return 0;
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
    : device_(std::make_unique<CaptureDevice>())
{
    device_->open(QIODevice::WriteOnly);
    QObject::connect(&AudioRoute::instance(), &AudioRoute::inputChanged, device_.get(),
        [this]() { follow(); });
}

QtAudioSource::~QtAudioSource()
{
    stop();
}

namespace {

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
    std::string failure;
    onOwnerThread(device_.get(), [this, &failure]() {
        device_->setRunning(true);
        try {
            open();
        } catch (const std::exception& error) {
            failure = error.what();
        }
    });
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
}

void QtAudioSource::open()
{
    const QAudioDevice input = AudioRoute::instance().input();
    if (input.isNull()) {
        throw std::runtime_error("no microphone: this system offers no audio input");
    }
    source_ = std::make_unique<QAudioSource>(input, callAudioFormat());
    source_->start(device_.get());
    if (source_->error() != QAudio::NoError) {
        throw std::runtime_error("the microphone did not start ("
            + input.description().toStdString() + "): audio error "
            + std::to_string(static_cast<int>(source_->error())));
    }
    bazarish::log::info("capture started on {}", input.description().toStdString());
}

void QtAudioSource::follow()
{
    if (!source_) {
        return;
    }
    source_->stop();
    source_.reset();
    try {
        open();
    } catch (const std::exception& error) {
        bazarish::log::warn("capture did not move: {}", error.what());
    }
}

void QtAudioSource::stop()
{
    device_->setRunning(false);
    onOwnerThread(device_.get(), [this]() {
        if (source_) {
            source_->stop();
            source_.reset();
        }
    });
}

std::vector<std::int16_t> QtAudioSource::readFrame()
{
    return device_->popFrame();
}

class QtAudioSink::PlaybackDevice : public QIODevice {
public:
    qint64 writeData(const char*, qint64) override
    {
        return 0;
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
    : device_(std::make_unique<PlaybackDevice>())
{
    device_->open(QIODevice::ReadOnly);
    QObject::connect(&AudioRoute::instance(), &AudioRoute::outputChanged, device_.get(),
        [this]() { follow(); });
}

QtAudioSink::~QtAudioSink()
{
    stop();
}

void QtAudioSink::start()
{
    std::string failure;
    onOwnerThread(device_.get(), [this, &failure]() {
        try {
            open();
        } catch (const std::exception& error) {
            failure = error.what();
        }
    });
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
}

void QtAudioSink::open()
{
    const QAudioDevice output = AudioRoute::instance().output();
    if (output.isNull()) {
        throw std::runtime_error("no speaker: this system offers no audio output");
    }
    sink_ = std::make_unique<QAudioSink>(output, callAudioFormat());
    sink_->start(device_.get());
    if (sink_->error() != QAudio::NoError) {
        throw std::runtime_error("the speaker did not start ("
            + output.description().toStdString() + "): audio error "
            + std::to_string(static_cast<int>(sink_->error())));
    }
    bazarish::log::info("playback started on {}", output.description().toStdString());
}

void QtAudioSink::follow()
{
    if (!sink_) {
        return;
    }
    sink_->stop();
    sink_.reset();
    try {
        open();
    } catch (const std::exception& error) {
        bazarish::log::warn("playback did not move: {}", error.what());
    }
}

void QtAudioSink::stop()
{
    onOwnerThread(device_.get(), [this]() {
        if (sink_) {
            sink_->stop();
            sink_.reset();
        }
    });
}

void QtAudioSink::writeFrame(const std::vector<std::int16_t>& pcm)
{
    device_->push(pcm);
}

}  // namespace bazarish::app
