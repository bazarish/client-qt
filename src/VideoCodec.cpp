// Bazarish project (c) 2026
#include "VideoCodec.hpp"

#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_image.h>

#include <cstring>
#include <stdexcept>

namespace bazarish {

namespace {

// VP8 speed/quality trade-off: positive values trade quality for encoder speed.
// A high value keeps a low-resolution realtime stream cheap on the CPU, which is
// what matters more than fidelity over a thin I2P link.
constexpr int kCpuUsed = 8;

int chromaWidth(const int width)
{
    return (width + 1) / 2;
}

int chromaHeight(const int height)
{
    return (height + 1) / 2;
}

// Copies one plane from a tightly packed source (stride == width) into a vpx
// plane that may have a wider, alignment-padded stride.
void copyPlaneIn(unsigned char* dst, const int dstStride, const unsigned char* src,
    const int planeWidth, const int planeHeight)
{
    for (int row = 0; row < planeHeight; ++row) {
        std::memcpy(dst + static_cast<std::size_t>(row) * dstStride,
            src + static_cast<std::size_t>(row) * planeWidth, static_cast<std::size_t>(planeWidth));
    }
}

// Copies one plane out of a vpx image (padded stride) into a tightly packed
// destination (stride == width).
void copyPlaneOut(unsigned char* dst, const unsigned char* src, const int srcStride,
    const int planeWidth, const int planeHeight)
{
    for (int row = 0; row < planeHeight; ++row) {
        std::memcpy(dst + static_cast<std::size_t>(row) * planeWidth,
            src + static_cast<std::size_t>(row) * srcStride, static_cast<std::size_t>(planeWidth));
    }
}

}  // namespace

std::size_t i420Size(const int width, const int height)
{
    const std::size_t luma = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t chroma = static_cast<std::size_t>(chromaWidth(width))
        * static_cast<std::size_t>(chromaHeight(height));
    return luma + 2 * chroma;
}

VideoEncoder::VideoEncoder(const int width, const int height, const int fps, const int bitrateKbps)
    : width_(width)
    , height_(height)
    , codec_(nullptr)
    , image_(nullptr)
    , pts_(0)
{
    if (width <= 0 || height <= 0 || (width % 2) != 0 || (height % 2) != 0) {
        throw std::invalid_argument("VideoEncoder: width and height must be positive and even");
    }

    vpx_codec_enc_cfg_t cfg;
    if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) {
        throw std::runtime_error("vpx encoder config defaults failed");
    }
    cfg.g_w = static_cast<unsigned int>(width);
    cfg.g_h = static_cast<unsigned int>(height);
    cfg.g_timebase.num = 1;
    cfg.g_timebase.den = fps;
    cfg.rc_target_bitrate = static_cast<unsigned int>(bitrateKbps);
    cfg.rc_end_usage = VPX_CBR;  // constant bitrate fits a bandwidth-bounded link
    cfg.g_pass = VPX_RC_ONE_PASS;
    cfg.g_lag_in_frames = 0;  // no lookahead: emit each frame immediately
    cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;  // survive datagram loss
    cfg.g_threads = 1;
    cfg.kf_mode = VPX_KF_AUTO;

    auto* const codec = new vpx_codec_ctx_t();
    if (vpx_codec_enc_init(codec, vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) {
        delete codec;
        throw std::runtime_error("vpx encoder init failed");
    }
    codec_ = codec;
    vpx_codec_control(codec, VP8E_SET_CPUUSED, kCpuUsed);
    vpx_codec_control(codec, VP8E_SET_ENABLEAUTOALTREF, 0);  // one packet per encode

    image_ = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, static_cast<unsigned int>(width),
        static_cast<unsigned int>(height), 32);
    if (image_ == nullptr) {
        vpx_codec_destroy(codec);
        delete codec;
        codec_ = nullptr;
        throw std::runtime_error("vpx image allocation failed");
    }
}

VideoEncoder::~VideoEncoder()
{
    if (image_ != nullptr) {
        vpx_img_free(static_cast<vpx_image_t*>(image_));
    }
    if (codec_ != nullptr) {
        auto* const codec = static_cast<vpx_codec_ctx_t*>(codec_);
        vpx_codec_destroy(codec);
        delete codec;
    }
}

VideoEncoder::VideoEncoder(VideoEncoder&& other) noexcept
    : width_(other.width_)
    , height_(other.height_)
    , codec_(other.codec_)
    , image_(other.image_)
    , pts_(other.pts_)
{
    other.codec_ = nullptr;
    other.image_ = nullptr;
}

VideoEncoder& VideoEncoder::operator=(VideoEncoder&& other) noexcept
{
    if (this != &other) {
        if (image_ != nullptr) {
            vpx_img_free(static_cast<vpx_image_t*>(image_));
        }
        if (codec_ != nullptr) {
            auto* const codec = static_cast<vpx_codec_ctx_t*>(codec_);
            vpx_codec_destroy(codec);
            delete codec;
        }
        width_ = other.width_;
        height_ = other.height_;
        codec_ = other.codec_;
        image_ = other.image_;
        pts_ = other.pts_;
        other.codec_ = nullptr;
        other.image_ = nullptr;
    }
    return *this;
}

EncodedVideoFrame VideoEncoder::encode(const VideoFrame& frame, const bool forceKeyframe)
{
    if (frame.width != width_ || frame.height != height_
        || frame.i420.size() != i420Size(width_, height_)) {
        throw std::invalid_argument("VideoEncoder: frame geometry does not match encoder");
    }
    auto* const img = static_cast<vpx_image_t*>(image_);
    const int cw = chromaWidth(width_);
    const int ch = chromaHeight(height_);
    const unsigned char* const src = frame.i420.data();
    copyPlaneIn(img->planes[VPX_PLANE_Y], img->stride[VPX_PLANE_Y], src, width_, height_);
    copyPlaneIn(img->planes[VPX_PLANE_U], img->stride[VPX_PLANE_U],
        src + static_cast<std::size_t>(width_) * height_, cw, ch);
    copyPlaneIn(img->planes[VPX_PLANE_V], img->stride[VPX_PLANE_V],
        src + static_cast<std::size_t>(width_) * height_ + static_cast<std::size_t>(cw) * ch, cw,
        ch);

    auto* const codec = static_cast<vpx_codec_ctx_t*>(codec_);
    const vpx_enc_frame_flags_t flags = forceKeyframe ? VPX_EFLAG_FORCE_KF : 0;
    if (vpx_codec_encode(codec, img, pts_, 1, flags, VPX_DL_REALTIME) != VPX_CODEC_OK) {
        throw std::runtime_error("vpx encode failed");
    }
    ++pts_;

    EncodedVideoFrame out;
    vpx_codec_iter_t iter = nullptr;
    const vpx_codec_cx_pkt_t* pkt = nullptr;
    while ((pkt = vpx_codec_get_cx_data(codec, &iter)) != nullptr) {
        if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) {
            continue;
        }
        const auto* const buf = static_cast<const std::uint8_t*>(pkt->data.frame.buf);
        out.data.assign(buf, buf + pkt->data.frame.sz);
        out.keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
    }
    return out;
}

VideoDecoder::VideoDecoder()
    : codec_(nullptr)
{
    vpx_codec_dec_cfg_t cfg;
    cfg.threads = 1;
    cfg.w = 0;
    cfg.h = 0;
    auto* const codec = new vpx_codec_ctx_t();
    if (vpx_codec_dec_init(codec, vpx_codec_vp8_dx(), &cfg, 0) != VPX_CODEC_OK) {
        delete codec;
        throw std::runtime_error("vpx decoder init failed");
    }
    codec_ = codec;
}

VideoDecoder::~VideoDecoder()
{
    if (codec_ != nullptr) {
        auto* const codec = static_cast<vpx_codec_ctx_t*>(codec_);
        vpx_codec_destroy(codec);
        delete codec;
    }
}

VideoDecoder::VideoDecoder(VideoDecoder&& other) noexcept
    : codec_(other.codec_)
{
    other.codec_ = nullptr;
}

VideoDecoder& VideoDecoder::operator=(VideoDecoder&& other) noexcept
{
    if (this != &other) {
        if (codec_ != nullptr) {
            auto* const codec = static_cast<vpx_codec_ctx_t*>(codec_);
            vpx_codec_destroy(codec);
            delete codec;
        }
        codec_ = other.codec_;
        other.codec_ = nullptr;
    }
    return *this;
}

VideoFrame VideoDecoder::decode(const Bytes& compressed)
{
    auto* const codec = static_cast<vpx_codec_ctx_t*>(codec_);
    if (vpx_codec_decode(codec, compressed.data(), static_cast<unsigned int>(compressed.size()),
            nullptr, 0)
        != VPX_CODEC_OK) {
        return VideoFrame{};  // corrupt packet: drop, caller may request a keyframe
    }

    VideoFrame out;
    vpx_codec_iter_t iter = nullptr;
    const vpx_image_t* const img = vpx_codec_get_frame(codec, &iter);
    if (img == nullptr) {
        return out;  // no displayable frame yet (e.g. waiting for a reference)
    }
    const int width = static_cast<int>(img->d_w);
    const int height = static_cast<int>(img->d_h);
    const int cw = chromaWidth(width);
    const int ch = chromaHeight(height);
    out.width = width;
    out.height = height;
    out.i420.resize(i420Size(width, height));
    unsigned char* const dst = out.i420.data();
    copyPlaneOut(dst, img->planes[VPX_PLANE_Y], img->stride[VPX_PLANE_Y], width, height);
    copyPlaneOut(dst + static_cast<std::size_t>(width) * height, img->planes[VPX_PLANE_U],
        img->stride[VPX_PLANE_U], cw, ch);
    copyPlaneOut(dst + static_cast<std::size_t>(width) * height + static_cast<std::size_t>(cw) * ch,
        img->planes[VPX_PLANE_V], img->stride[VPX_PLANE_V], cw, ch);
    return out;
}

}  // namespace bazarish
