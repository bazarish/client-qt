// Bazarish project (c) 2026
#include "AudioCodec.hpp"

#include <opus/opus.h>

#include <stdexcept>

namespace bazarish {

namespace {

// Opus packets for one 20 ms mono frame are well under this; the buffer only
// bounds a single encode call.
constexpr int kMaxPacketBytes = 4000;

}  // namespace

AudioEncoder::AudioEncoder()
    : encoder_(nullptr)
{
    int error = 0;
    encoder_ = opus_encoder_create(kCallSampleRate, kCallChannels, OPUS_APPLICATION_VOIP, &error);
    if (encoder_ == nullptr || error != OPUS_OK) {
        throw std::runtime_error("opus encoder creation failed");
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

}  // namespace bazarish
