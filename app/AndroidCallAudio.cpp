// Bazarish project (c) 2026
#include "AndroidCallAudio.hpp"

#include "AudioCodec.hpp"

#include <QJniEnvironment>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bazarish::app {
namespace {

const char* const kCallAudio = "sh/bazari/client/CallAudio";
constexpr int kFrameBytes = kCallSamplesPerFrame * static_cast<int>(sizeof(std::int16_t));

QJniObject directBuffer(std::vector<std::int16_t>& frame)
{
    QJniEnvironment env;
    jobject const local = env->NewDirectByteBuffer(frame.data(), kFrameBytes);
    if (local == nullptr) {
        throw std::runtime_error("the call audio buffer could not be shared with Java");
    }
    return QJniObject::fromLocalRef(local);
}

}  // namespace

AndroidCallSource::AndroidCallSource()
    : frame_(static_cast<std::size_t>(kCallSamplesPerFrame))
    , buffer_(directBuffer(frame_))
{
}

AndroidCallSource::~AndroidCallSource()
{
    stop();
}

void AndroidCallSource::start()
{
    QJniObject::callStaticMethod<void>(kCallAudio, "startCapture", "(II)V",
        jint(kCallSampleRate), jint(kFrameBytes));
    running_.store(true);
}

void AndroidCallSource::stop()
{
    if (running_.exchange(false)) {
        QJniObject::callStaticMethod<void>(kCallAudio, "stopCapture", "()V");
    }
}

std::vector<std::int16_t> AndroidCallSource::readFrame()
{
    if (!running_.load()) {
        return {};
    }
    const jint read = QJniObject::callStaticMethod<jint>(kCallAudio, "read",
        "(Ljava/nio/ByteBuffer;I)I", buffer_.object(), jint(kFrameBytes));
    if (read != kFrameBytes) {
        return {};
    }
    return frame_;
}

AndroidCallSink::AndroidCallSink()
    : frame_(static_cast<std::size_t>(kCallSamplesPerFrame))
    , buffer_(directBuffer(frame_))
{
}

AndroidCallSink::~AndroidCallSink()
{
    stop();
}

void AndroidCallSink::start()
{
    QJniObject::callStaticMethod<void>(kCallAudio, "startPlayback", "(II)V",
        jint(kCallSampleRate), jint(kFrameBytes));
    running_.store(true);
}

void AndroidCallSink::stop()
{
    if (running_.exchange(false)) {
        QJniObject::callStaticMethod<void>(kCallAudio, "stopPlayback", "()V");
    }
}

void AndroidCallSink::writeFrame(const std::vector<std::int16_t>& pcm)
{
    if (!running_.load()) {
        return;
    }
    if (pcm.size() > frame_.size()) {
        throw std::runtime_error("a call audio frame is longer than one frame");
    }
    std::copy(pcm.begin(), pcm.end(), frame_.begin());
    const jint bytes = static_cast<jint>(pcm.size() * sizeof(std::int16_t));
    QJniObject::callStaticMethod<void>(kCallAudio, "write", "(Ljava/nio/ByteBuffer;I)V",
        buffer_.object(), bytes);
}

namespace AndroidCallAudio {

void setSpeaker(const bool on)
{
    QJniObject::callStaticMethod<void>(kCallAudio, "setSpeaker", "(Z)V", jboolean(on));
}

}  // namespace AndroidCallAudio

}  // namespace bazarish::app
