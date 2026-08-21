// Bazarish project (c) 2026
#include "AudioCodec.hpp"

#include <opus/opus.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bazarish {

namespace {

// Opus packets for one 20 ms mono frame are well under this; the buffer only
// bounds a single encode call.
constexpr int kMaxPacketBytes = 4000;

// Full scale of a signed-16 sample, the reference every level is measured
// against.
constexpr double kFullScale = 32768.0;

}  // namespace

AudioEncoder::AudioEncoder(const int bitrateBps)
    : encoder_(nullptr)
{
    int error = 0;
    encoder_ = opus_encoder_create(kCallSampleRate, kCallChannels, OPUS_APPLICATION_VOIP, &error);
    if (encoder_ == nullptr || error != OPUS_OK) {
        throw std::runtime_error("opus encoder creation failed");
    }
    if (bitrateBps > 0 && opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrateBps)) != OPUS_OK) {
        opus_encoder_destroy(encoder_);
        throw std::runtime_error("opus encoder rejected the bitrate");
    }
}

AudioEncoder::~AudioEncoder()
{
    if (encoder_ != nullptr) {
        opus_encoder_destroy(encoder_);
    }
}

AudioEncoder::AudioEncoder(AudioEncoder&& other) noexcept
    : encoder_(other.encoder_)
{
    other.encoder_ = nullptr;
}

AudioEncoder& AudioEncoder::operator=(AudioEncoder&& other) noexcept
{
    if (this != &other) {
        if (encoder_ != nullptr) {
            opus_encoder_destroy(encoder_);
        }
        encoder_ = other.encoder_;
        other.encoder_ = nullptr;
    }
    return *this;
}

Bytes AudioEncoder::encode(const std::int16_t* pcm, const int samples)
{
    Bytes packet(kMaxPacketBytes);
    const opus_int32 written
        = opus_encode(encoder_, pcm, samples, packet.data(), kMaxPacketBytes);
    if (written < 0) {
        throw std::runtime_error("opus encode failed");
    }
    packet.resize(static_cast<std::size_t>(written));
    return packet;
}

AudioDecoder::AudioDecoder()
    : decoder_(nullptr)
{
    int error = 0;
    decoder_ = opus_decoder_create(kCallSampleRate, kCallChannels, &error);
    if (decoder_ == nullptr || error != OPUS_OK) {
        throw std::runtime_error("opus decoder creation failed");
    }
}

AudioDecoder::~AudioDecoder()
{
    if (decoder_ != nullptr) {
        opus_decoder_destroy(decoder_);
    }
}

AudioDecoder::AudioDecoder(AudioDecoder&& other) noexcept
    : decoder_(other.decoder_)
{
    other.decoder_ = nullptr;
}

AudioDecoder& AudioDecoder::operator=(AudioDecoder&& other) noexcept
{
    if (this != &other) {
        if (decoder_ != nullptr) {
            opus_decoder_destroy(decoder_);
        }
        decoder_ = other.decoder_;
        other.decoder_ = nullptr;
    }
    return *this;
}

std::vector<std::int16_t> AudioDecoder::decode(const Bytes& packet)
{
    std::vector<std::int16_t> pcm(kCallSamplesPerFrame);
    // An empty packet signals a lost frame: pass null/0 so Opus runs PLC.
    const unsigned char* const data = packet.empty() ? nullptr : packet.data();
    const opus_int32 dataSize = static_cast<opus_int32>(packet.size());
    const int samples = opus_decode(decoder_, data, dataSize, pcm.data(), kCallSamplesPerFrame, 0);
    if (samples < 0) {
        throw std::runtime_error("opus decode failed");
    }
    pcm.resize(static_cast<std::size_t>(samples));
    return pcm;
}

namespace {

// Each frame is preceded by its length. Opus frames at these settings are well
// under a kilobyte, so two bytes carry any of them.
constexpr std::size_t kFrameLengthBytes = 2;
constexpr unsigned kByteBits = 8;
constexpr std::size_t kMaxFrameBytes = 0xFFFF;

}  // namespace

Bytes packOpusFrames(const std::vector<Bytes>& frames)
{
    Bytes packed;
    for (const Bytes& frame : frames) {
        if (frame.empty() || frame.size() > kMaxFrameBytes) {
            throw std::runtime_error("an Opus frame of an impossible size");
        }
        packed.push_back(static_cast<unsigned char>(frame.size() >> kByteBits));
        packed.push_back(static_cast<unsigned char>(frame.size() & 0xFF));
        packed.insert(packed.end(), frame.begin(), frame.end());
    }
    return packed;
}

std::vector<std::uint8_t> voiceWaveform(const Bytes& packed, const int bars)
{
    if (bars <= 0) {
        return {};
    }
    const std::vector<Bytes> frames = unpackOpusFrames(packed);
    if (frames.empty()) {
        return {};
    }
    // One RMS per 20 ms frame first, then frames folded into bars: the fold is
    // over a whole number of frames however long the message is.
    AudioDecoder decoder;
    std::vector<double> frameLevels;
    frameLevels.reserve(frames.size());
    for (const Bytes& frame : frames) {
        const std::vector<std::int16_t> pcm = decoder.decode(frame);
        double sum = 0.0;
        for (const std::int16_t sample : pcm) {
            const double value = static_cast<double>(sample) / kFullScale;
            sum += value * value;
        }
        frameLevels.push_back(pcm.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(pcm.size())));
    }

    std::vector<double> barLevels(static_cast<std::size_t>(bars), 0.0);
    for (std::size_t i = 0; i < frameLevels.size(); ++i) {
        const std::size_t bar
            = i * static_cast<std::size_t>(bars) / frameLevels.size();
        barLevels[bar] = std::max(barLevels[bar], frameLevels[i]);
    }
    const double peak = *std::max_element(barLevels.begin(), barLevels.end());
    std::vector<std::uint8_t> out(static_cast<std::size_t>(bars), 0);
    if (peak < kWaveformSilence) {
        return out;  // nothing was recorded loud enough to draw
    }
    for (std::size_t i = 0; i < barLevels.size(); ++i) {
        const double scaled = barLevels[i] / peak * (kWaveformLevels - 1);
        out[i] = static_cast<std::uint8_t>(std::lround(scaled));
    }
    return out;
}

std::vector<Bytes> unpackOpusFrames(const Bytes& packed)
{
    std::vector<Bytes> frames;
    std::size_t at = 0;
    while (at + kFrameLengthBytes <= packed.size()) {
        const std::size_t length = (static_cast<std::size_t>(packed[at]) << kByteBits)
            | static_cast<std::size_t>(packed[at + 1]);
        at += kFrameLengthBytes;
        if (length == 0 || at + length > packed.size()) {
            throw std::runtime_error("voice audio ends mid-frame");
        }
        frames.emplace_back(packed.begin() + static_cast<std::ptrdiff_t>(at),
            packed.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
    }
    if (at != packed.size()) {
        throw std::runtime_error("voice audio has trailing bytes");
    }
    return frames;
}

}  // namespace bazarish
