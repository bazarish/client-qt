// Bazarish project (c) 2026
#include "VideoCodec.hpp"
#include "VideoIo.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

// Mean absolute difference of the luma planes of two same-geometry frames.
double lumaMeanAbsDiff(const VideoFrame& a, const VideoFrame& b)
{
    const std::size_t luma = static_cast<std::size_t>(a.width) * a.height;
    double sum = 0.0;
    for (std::size_t i = 0; i < luma; ++i) {
        sum += std::abs(static_cast<int>(a.i420[i]) - static_cast<int>(b.i420[i]));
    }
    return sum / static_cast<double>(luma);
}

// Builds a deterministic I420 frame with a diagonal luma ramp and neutral chroma.
VideoFrame makeFrame(const int width, const int height, const std::uint32_t t)
{
    VideoFrame frame;
    frame.width = width;
    frame.height = height;
    frame.i420.resize(i420Size(width, height));
    unsigned char* p = frame.i420.data();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            *p++ = static_cast<unsigned char>((x + y + t) & 0xff);
        }
    }
    const std::size_t chroma = frame.i420.size() - static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < chroma; ++i) {
        *p++ = 128;
    }
    return frame;
}

void testI420Size()
{
    CHECK(i420Size(320, 240) == 320u * 240u * 3 / 2);
    CHECK(i420Size(2, 2) == 6u);  // 4 luma + 1 U + 1 V
}

// A forced keyframe round-trips: the decoder recovers a frame of the same
// geometry whose luma is close to the source (VP8 is lossy but a keyframe at a
// sane bitrate stays well within tolerance).
void testKeyframeRoundTrip()
{
    const int w = 320;
    const int h = 240;
    VideoEncoder encoder(w, h, kCallVideoFps, kCallVideoBitrateKbps);
    VideoDecoder decoder;

    const VideoFrame source = makeFrame(w, h, 0);
    const EncodedVideoFrame encoded = encoder.encode(source, true);
    CHECK(encoded.valid());
    CHECK(encoded.keyframe);

    const VideoFrame decoded = decoder.decode(encoded.data);
    CHECK(decoded.valid());
    CHECK(decoded.width == w);
    CHECK(decoded.height == h);
    CHECK(decoded.i420.size() == i420Size(w, h));

    const double diff = lumaMeanAbsDiff(source, decoded);
    std::fprintf(stderr, "keyframe luma mean-abs-diff: %.2f\n", diff);
    CHECK(diff < 20.0);
}

// A sequence of frames encodes and decodes: the first forced frame is a
// keyframe, later frames are inter frames, and each decodes to a valid image.
void testSequence()
{
    const int w = 320;
    const int h = 240;
    VideoEncoder encoder(w, h, kCallVideoFps, kCallVideoBitrateKbps);
    VideoDecoder decoder;

    for (std::uint32_t i = 0; i < 10; ++i) {
        const VideoFrame source = makeFrame(w, h, i);
        const EncodedVideoFrame encoded = encoder.encode(source, i == 0);
        CHECK(encoded.valid());
        CHECK(encoded.keyframe == (i == 0));
        const VideoFrame decoded = decoder.decode(encoded.data);
        CHECK(decoded.valid());
        CHECK(decoded.width == w && decoded.height == h);
    }
}

// An inter frame arriving with no prior reference produces no output rather than
// crashing, and a corrupt packet is rejected without throwing.
void testInterFrameWithoutReference()
{
    const int w = 320;
    const int h = 240;
    VideoEncoder encoder(w, h, kCallVideoFps, kCallVideoBitrateKbps);

    // Prime the encoder with a keyframe so the next frame is an inter frame, then
    // hand only the inter frame to a fresh decoder that has never seen a keyframe.
    encoder.encode(makeFrame(w, h, 0), true);
    const EncodedVideoFrame inter = encoder.encode(makeFrame(w, h, 1), false);
    CHECK(inter.valid());
    CHECK(!inter.keyframe);

    VideoDecoder decoder;
    const VideoFrame decoded = decoder.decode(inter.data);
    CHECK(!decoded.valid());  // no reference yet: nothing to show, but no crash

    const Bytes garbage = {0x00, 0x11, 0x22, 0x33, 0x44};
    const VideoFrame fromGarbage = decoder.decode(garbage);
    CHECK(!fromGarbage.valid());
}

// The synthetic camera produces real frames of the requested geometry that the
// encoder accepts.
void testPatternSourceFeedsEncoder()
{
    PatternVideoSource source(320, 240, kCallVideoFps);
    VideoEncoder encoder(320, 240, kCallVideoFps, kCallVideoBitrateKbps);
    source.start();
    const VideoFrame frame = source.readFrame();
    CHECK(frame.valid());
    CHECK(frame.width == 320 && frame.height == 240);
    CHECK(frame.i420.size() == i420Size(320, 240));
    const EncodedVideoFrame encoded = encoder.encode(frame, true);
    CHECK(encoded.valid());
    source.stop();
}

}  // namespace

int main()
{
    testI420Size();
    testKeyframeRoundTrip();
    testSequence();
    testInterFrameWithoutReference();
    testPatternSourceFeedsEncoder();
    std::fprintf(stderr, "TestVideoCodec: all checks passed\n");
    return 0;
}
