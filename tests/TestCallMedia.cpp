// Bazarish project (c) 2026
#include "AudioCodec.hpp"
#include "AudioIo.hpp"
#include "CallMedia.hpp"

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
#include <algorithm>
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
// loopback link standing in for two I2P datagram endpoints.
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

    // 1b) The waveform a voice bubble draws comes from the audio itself: a tone
    //     that plays only in the second half draws a quiet first half and a loud
    //     second one, and pure silence draws flat.
    {
        constexpr int kBars = 8;
        constexpr int kFramesPerHalf = 25;  // half a second at 20 ms a frame
        AudioEncoder encoder;
        SineAudioSource source(440.0);
        source.start();
        const std::vector<std::int16_t> silence(kCallSamplesPerFrame, 0);
        std::vector<Bytes> frames;
        for (int i = 0; i < kFramesPerHalf; ++i) {
            frames.push_back(encoder.encode(silence.data(), kCallSamplesPerFrame));
        }
        for (int i = 0; i < kFramesPerHalf; ++i) {
            const std::vector<std::int16_t> tone = source.readFrame();
            frames.push_back(encoder.encode(tone.data(), kCallSamplesPerFrame));
        }
        source.stop();

        const std::vector<std::uint8_t> wave = voiceWaveform(packOpusFrames(frames), kBars);
        CHECK(wave.size() == static_cast<std::size_t>(kBars));
        CHECK(wave.front() < wave.back());
        CHECK(wave.back() == kWaveformLevels - 1);  // the loudest slice tops out

        // Its own encoder: a codec stream carries the tail of what came before,
        // and every recording starts one of its own.
        AudioEncoder quietEncoder;
        std::vector<Bytes> quiet;
        for (int i = 0; i < kFramesPerHalf; ++i) {
            quiet.push_back(quietEncoder.encode(silence.data(), kCallSamplesPerFrame));
        }
        const std::vector<std::uint8_t> flat = voiceWaveform(packOpusFrames(quiet), kBars);
        CHECK(flat.size() == static_cast<std::size_t>(kBars));
        CHECK(std::all_of(flat.begin(), flat.end(), [](std::uint8_t bar) { return bar == 0; }));
    }

    // 1c) Faster playback keeps the voice: a 440 Hz tone played at 2x comes out
    //     half as long, and still 440 Hz. Pitch is counted by zero crossings,
    //     which is what would double if the audio were merely resampled.
    {
        constexpr double kSpeed = 2.0;
        constexpr int kToneFrames = 100;  // two seconds
        constexpr double kToneHz = 440.0;
        // A tone rings either side of zero twice a period.
        constexpr double kCrossingsPerPeriod = 2.0;
        constexpr double kPitchTolerance = 0.05;
        constexpr double kLengthTolerance = 0.05;

        SineAudioSource source(kToneHz);
        source.start();
        std::vector<std::int16_t> tone;
        for (int i = 0; i < kToneFrames; ++i) {
            const std::vector<std::int16_t> frame = source.readFrame();
            tone.insert(tone.end(), frame.begin(), frame.end());
        }
        source.stop();

        TimeStretch stretch(tone, kSpeed);
        std::vector<std::int16_t> fast;
        std::vector<std::int16_t> chunk(kCallSamplesPerFrame);
        for (std::size_t produced = stretch.read(chunk.data(), chunk.size()); produced > 0;
            produced = stretch.read(chunk.data(), chunk.size())) {
            fast.insert(fast.end(), chunk.begin(),
                chunk.begin() + static_cast<std::ptrdiff_t>(produced));
        }

        // Half the samples, within a window's worth of rounding.
        const double ratio = static_cast<double>(fast.size()) / static_cast<double>(tone.size());
        CHECK(std::abs(ratio - 1.0 / kSpeed) < kLengthTolerance);

        // And the same pitch: crossings per second, not per sample.
        const auto pitchOf = [](const std::vector<std::int16_t>& pcm) {
            int crossings = 0;
            for (std::size_t i = 1; i < pcm.size(); ++i) {
                if ((pcm[i - 1] < 0) != (pcm[i] < 0)) {
                    ++crossings;
                }
            }
            return crossings * static_cast<double>(kCallSampleRate)
                / (static_cast<double>(pcm.size()) * kCrossingsPerPeriod);
        };
        const double heard = pitchOf(fast);
        CHECK(std::abs(heard - kToneHz) / kToneHz < kPitchTolerance);
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
            std::move(callerSink), key, CallRole::eCaller);
        CallMedia callee(calleeTransport, std::make_unique<SineAudioSource>(660.0),
            std::move(calleeSink), key, CallRole::eCallee);

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
            std::make_unique<CapturingAudioSink>(), callerKey, CallRole::eCaller);
        CallMedia callee(calleeTransport, std::make_unique<SineAudioSource>(440.0),
            std::move(calleeSink), calleeKey, CallRole::eCallee);

        caller.start();
        callee.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        caller.stop();
        callee.stop();

        CHECK(caller.packetsSent() >= 5);       // datagrams really were sent
        CHECK(calleeSinkRaw->frameCount() == 0);  // none opened with the wrong key
        CHECK(callee.packetsReceived() == 0);
    }

    std::printf("TestCallMedia OK\n");
    return 0;
}
