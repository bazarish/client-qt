// Bazarish project (c) 2026
#include "AndroidCallAudio.hpp"

#include "AudioCodec.hpp"

#include <QJniEnvironment>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace bazarish::app {
namespace {

const char* const kCallAudio = "sh/bazari/client/CallAudio";
const char* const kCapture = "sh/bazari/client/CallAudio$Capture";
const char* const kPlayback = "sh/bazari/client/CallAudio$Playback";
const char* const kVoicePlayback = "sh/bazari/client/VoicePlayback";
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
    capture_ = QJniObject(kCapture, "(II)V", jint(kCallSampleRate), jint(kFrameBytes));
    if (!capture_.isValid()) {
        throw std::runtime_error("the microphone did not open for the call");
    }
    running_.store(true);
}

void AndroidCallSource::stop()
{
    if (running_.exchange(false)) {
        capture_.callMethod<void>("stop", "()V");
    }
}

std::vector<std::int16_t> AndroidCallSource::readFrame()
{
    if (!running_.load()) {
        return {};
    }
    const jint read = capture_.callMethod<jint>(
        "read", "(Ljava/nio/ByteBuffer;I)I", buffer_.object(), jint(kFrameBytes));
    if (read != kFrameBytes) {
        return {};
    }
    return frame_;
}

AndroidCallSink::AndroidCallSink()
    : AndroidCallSink(kPlayback)
{
}

AndroidCallSink::AndroidCallSink(const char* const playbackClass)
    : playbackClass_(playbackClass)
    , frame_(static_cast<std::size_t>(kCallSamplesPerFrame))
    , buffer_(directBuffer(frame_))
{
}

AndroidVoiceSink::AndroidVoiceSink()
    : AndroidCallSink(kVoicePlayback)
{
}

AndroidCallSink::~AndroidCallSink()
{
    stop();
}

void AndroidCallSink::start()
{
    playback_ = QJniObject(playbackClass_, "(II)V", jint(kCallSampleRate), jint(kFrameBytes));
    if (!playback_.isValid()) {
        throw std::runtime_error(std::string("the audio did not open: ") + playbackClass_);
    }
    running_.store(true);
}

void AndroidCallSink::stop()
{
    if (running_.exchange(false)) {
        playback_.callMethod<void>("stop", "()V");
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
    playback_.callMethod<void>("write", "(Ljava/nio/ByteBuffer;I)V", buffer_.object(), bytes);
}

namespace AndroidCallAudio {

void setSpeaker(const bool on)
{
    QJniObject::callStaticMethod<void>(kCallAudio, "setSpeaker", "(Z)V", jboolean(on));
}

void holdNearEar(const bool on)
{
    QJniObject::callStaticMethod<void>(kCallAudio, "holdNearEar", "(Z)V", jboolean(on));
}

}  // namespace AndroidCallAudio

}  // namespace bazarish::app
