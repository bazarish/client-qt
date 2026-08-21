// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <cstdint>
#include <vector>

typedef struct OpusEncoder OpusEncoder;
typedef struct OpusDecoder OpusDecoder;

namespace bazarish {

// Call audio format: 48 kHz mono, 20 ms frames. Opus operates natively at these
// rates and a 20 ms frame is the standard VoIP trade-off between latency and
// per-packet overhead. One frame is kCallSamplesPerFrame signed-16 samples.
inline constexpr int kCallSampleRate = 48000;
inline constexpr int kCallChannels = 1;
inline constexpr int kCallFrameMs = 20;
inline constexpr int kCallSamplesPerFrame = kCallSampleRate / 1000 * kCallFrameMs;  // 960

// Opus encoder for one mono stream. Move-only; owns the codec state.
class AudioEncoder {
public:
    // bitrateBps of 0 leaves the codec its own choice, which is what a call
    // wants: it adapts to the link. A recording that has to fit inside one
    // message says what it may spend instead.
    explicit AudioEncoder(int bitrateBps = 0);
    ~AudioEncoder();

    AudioEncoder(AudioEncoder&& other) noexcept;
    AudioEncoder& operator=(AudioEncoder&& other) noexcept;
    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    // Encodes exactly kCallSamplesPerFrame samples into an Opus packet.
    Bytes encode(const std::int16_t* pcm, int samples);

private:
    OpusEncoder* encoder_;
};

// Opus decoder for one mono stream. Move-only; owns the codec state.
class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    AudioDecoder(AudioDecoder&& other) noexcept;
    AudioDecoder& operator=(AudioDecoder&& other) noexcept;
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    // Decodes one Opus packet into kCallSamplesPerFrame samples. An empty
    // packet invokes packet-loss concealment (a dropped frame is synthesised).
    std::vector<std::int16_t> decode(const Bytes& packet);

private:
    OpusDecoder* decoder_;
};

// A voice message is a run of Opus frames, each one length-prefixed, in the same
// 48 kHz mono 20 ms format calls use. There is no container beyond that: both
// ends of this protocol are this client, and an Ogg header would be bytes spent
// telling ourselves what we already know.
Bytes packOpusFrames(const std::vector<Bytes>& frames);
std::vector<Bytes> unpackOpusFrames(const Bytes& packed);

// How loud a voice message is over its length: one bar per slice, each in
// 0..kWaveformLevels-1, taken from the decoded audio rather than from anything
// stored alongside it. Loudness is relative to the recording's own peak, so a
// quiet recording still draws a shape - but audio that never rises above
// kWaveformSilence of full scale is silence, and draws flat. Meant to be
// computed once, when a message is stored.
inline constexpr int kWaveformLevels = 16;
inline constexpr double kWaveformSilence = 0.01;
std::vector<std::uint8_t> voiceWaveform(const Bytes& packed, int bars);

}  // namespace bazarish
