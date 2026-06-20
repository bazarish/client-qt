// Bazarish project (c) 2026
#include "AudioCodec.hpp"
#include "AudioIo.hpp"
#include "CallMedia.hpp"
#include "VideoCodec.hpp"
#include "VideoIo.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace bazarish;

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

namespace {

// A thread-safe in-memory datagram queue: one direction of a loopback link.
class LoopbackChannel {
public:
    void push(const std::vector<std::uint8_t>& packet)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(packet);
        }
        cv_.notify_one();
    }

    std::vector<std::uint8_t> pop(const int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                [this] { return !queue_.empty(); })) {
            return {};
        }
        std::vector<std::uint8_t> packet = std::move(queue_.front());
        queue_.pop_front();
        return packet;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::vector<std::uint8_t>> queue_;
};

// Sends into one channel, receives from the other; a pair forms a full-duplex
// loopback link standing in for two SAM datagram sessions.
class LoopbackTransport : public CallTransport {
public:
    LoopbackTransport(LoopbackChannel& out, LoopbackChannel& in)
        : out_(out)
        , in_(in)
    {
    }

    void sendDatagram(const void* data, const std::size_t size) override
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        out_.push(std::vector<std::uint8_t>(bytes, bytes + size));
    }

    std::vector<std::uint8_t> receiveDatagram(const int timeoutMs) override
    {
        return in_.pop(timeoutMs);
    }

private:
    LoopbackChannel& out_;
    LoopbackChannel& in_;
};

double frameEnergy(const std::vector<std::int16_t>& pcm)
{
    double sum = 0.0;
    for (const std::int16_t sample : pcm) {
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return pcm.empty() ? 0.0 : sum / static_cast<double>(pcm.size());
}

}  // namespace

int main()
{
    // 1) Opus round trip: a 440 Hz frame encodes to a compact packet and decodes
    //    back to a full frame whose energy is preserved (the tone survives).
    {
        SineAudioSource source(440.0);
        source.start();
        const std::vector<std::int16_t> frame = source.readFrame();
        CHECK(frame.size() == static_cast<std::size_t>(kCallSamplesPerFrame));
        CHECK(frameEnergy(frame) > 1000.0);

        AudioEncoder encoder;
        AudioDecoder decoder;
        const Bytes packet = encoder.encode(frame.data(), static_cast<int>(frame.size()));
        CHECK(!packet.empty());
        CHECK(packet.size() < frame.size() * sizeof(std::int16_t));  // actually compressed
        const std::vector<std::int16_t> decoded = decoder.decode(packet);
        CHECK(decoded.size() == static_cast<std::size_t>(kCallSamplesPerFrame));
        CHECK(frameEnergy(decoded) > 100.0);  // signal, not silence

        // An empty packet runs packet-loss concealment, still a full frame.
        const std::vector<std::int16_t> concealed = decoder.decode(Bytes{});
        CHECK(concealed.size() == static_cast<std::size_t>(kCallSamplesPerFrame));
        source.stop();
    }

    // 2) Full media pipeline over a loopback link: both sides capture a tone,
    //    seal/encode it, and the peer decrypts/decodes and plays real frames.
    {
        LoopbackChannel aToB;
        LoopbackChannel bToA;
        LoopbackTransport callerTransport(aToB, bToA);
        LoopbackTransport calleeTransport(bToA, aToB);

        const Bytes key(kAeadKeyBytes, 0x5a);
        auto callerSink = std::make_unique<CapturingAudioSink>();
        auto calleeSink = std::make_unique<CapturingAudioSink>();
        CapturingAudioSink* const callerSinkRaw = callerSink.get();
        CapturingAudioSink* const calleeSinkRaw = calleeSink.get();

        CallMedia caller(callerTransport, std::make_unique<SineAudioSource>(440.0),
            std::move(callerSink), nullptr, nullptr, key, CallRole::eCaller);
        CallMedia callee(calleeTransport, std::make_unique<SineAudioSource>(660.0),
            std::move(calleeSink), nullptr, nullptr, key, CallRole::eCallee);

        caller.start();
        callee.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        caller.stop();
        callee.stop();

        // ~20 frames each way in 400 ms; allow generous slack for scheduling.
        CHECK(caller.packetsSent() >= 5);
        CHECK(callee.packetsSent() >= 5);
        CHECK(callerSinkRaw->frameCount() >= 5);
        CHECK(calleeSinkRaw->frameCount() >= 5);
        // Played frames roughly track received datagrams (PLC may add a few).
        CHECK(caller.packetsReceived() >= 5);
        CHECK(callee.packetsReceived() >= 5);
    }

    // 3) AEAD gate: a peer with the wrong key receives datagrams but cannot open
    //    any, so nothing is ever played (a forged stream is silently dropped).
    {
        LoopbackChannel aToB;
        LoopbackChannel bToA;
        LoopbackTransport callerTransport(aToB, bToA);
        LoopbackTransport calleeTransport(bToA, aToB);

        const Bytes callerKey(kAeadKeyBytes, 0x11);
        const Bytes calleeKey(kAeadKeyBytes, 0x22);  // mismatched
        auto calleeSink = std::make_unique<CapturingAudioSink>();
        CapturingAudioSink* const calleeSinkRaw = calleeSink.get();

        CallMedia caller(callerTransport, std::make_unique<SineAudioSource>(440.0),
            std::make_unique<CapturingAudioSink>(), nullptr, nullptr, callerKey, CallRole::eCaller);
        CallMedia callee(calleeTransport, std::make_unique<SineAudioSource>(440.0),
            std::move(calleeSink), nullptr, nullptr, calleeKey, CallRole::eCallee);

        caller.start();
        callee.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        caller.stop();
        callee.stop();

        CHECK(caller.packetsSent() >= 5);       // datagrams really were sent
        CHECK(calleeSinkRaw->frameCount() == 0);  // none opened with the wrong key
        CHECK(callee.packetsReceived() == 0);
    }

    // 4) Full audio+video pipeline over loopback: both sides also capture video,
    //    fragment each VP8 frame across datagrams, and the peer reassembles,
    //    decodes and renders frames of the right geometry.
    {
        LoopbackChannel aToB;
        LoopbackChannel bToA;
        LoopbackTransport callerTransport(aToB, bToA);
        LoopbackTransport calleeTransport(bToA, aToB);

        const Bytes key(kAeadKeyBytes, 0x33);
        auto callerVideoSink = std::make_unique<CapturingVideoSink>(true);
        auto calleeVideoSink = std::make_unique<CapturingVideoSink>(true);
        CapturingVideoSink* const callerVideoRaw = callerVideoSink.get();
        CapturingVideoSink* const calleeVideoRaw = calleeVideoSink.get();

        CallMedia caller(callerTransport, std::make_unique<SineAudioSource>(440.0),
            std::make_unique<CapturingAudioSink>(), std::make_unique<PatternVideoSource>(),
            std::move(callerVideoSink), key, CallRole::eCaller);
        CallMedia callee(calleeTransport, std::make_unique<SineAudioSource>(660.0),
            std::make_unique<CapturingAudioSink>(), std::make_unique<PatternVideoSource>(),
            std::move(calleeVideoSink), key, CallRole::eCallee);
        CHECK(caller.hasVideo());
        CHECK(callee.hasVideo());

        caller.start();
        callee.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        caller.stop();
        callee.stop();

        // ~13 video frames each way at 15 fps in 900 ms; allow generous slack.
        CHECK(callerVideoRaw->frameCount() >= 3);
        CHECK(calleeVideoRaw->frameCount() >= 3);
        const VideoFrame received = calleeVideoRaw->lastFrame();
        CHECK(received.valid());
        CHECK(received.width == kCallVideoWidth);
        CHECK(received.height == kCallVideoHeight);
        CHECK(received.i420.size() == i420Size(kCallVideoWidth, kCallVideoHeight));
    }

    std::printf("TestCallMedia OK\n");
    return 0;
}
