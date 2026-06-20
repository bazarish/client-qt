// Bazarish project (c) 2026
#include "QtVideoIo.hpp"

#include "VideoCodec.hpp"

#include <QCamera>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <cstring>
#include <utility>

namespace bazarish::app {

using bazarish::VideoFrame;

namespace {

// Converts a scaled RGB888 image to packed I420. Chroma is point-sampled at the
// top-left of each 2x2 block, which is cheap and good enough for a low-resolution
// realtime stream.
VideoFrame imageToI420(const QImage& rgb)
{
    const int width = rgb.width();
    const int height = rgb.height();
    VideoFrame frame;
    frame.width = width;
    frame.height = height;
    frame.i420.resize(i420Size(width, height));
    unsigned char* const dst = frame.i420.data();
    unsigned char* const yPlane = dst;
    unsigned char* const uPlane = dst + static_cast<std::size_t>(width) * height;
    unsigned char* const vPlane = uPlane + static_cast<std::size_t>((width + 1) / 2) * ((height + 1) / 2);
    const int chromaW = (width + 1) / 2;

    for (int y = 0; y < height; ++y) {
        const uchar* const line = rgb.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const uchar* const px = line + x * 3;
            const int r = px[0];
            const int g = px[1];
            const int b = px[2];
            yPlane[static_cast<std::size_t>(y) * width + x]
                = static_cast<unsigned char>((77 * r + 150 * g + 29 * b) >> 8);
            if ((y % 2) == 0 && (x % 2) == 0) {
                const std::size_t ci
                    = static_cast<std::size_t>(y / 2) * chromaW + (x / 2);
                uPlane[ci] = static_cast<unsigned char>(((-43 * r - 84 * g + 127 * b) >> 8) + 128);
                vPlane[ci] = static_cast<unsigned char>(((127 * r - 106 * g - 21 * b) >> 8) + 128);
            }
        }
    }
    return frame;
}

// Wraps a packed I420 VideoFrame as a QVideoFrame the GUI can render.
QVideoFrame i420ToQVideoFrame(const VideoFrame& frame)
{
    QVideoFrameFormat format(
        QSize(frame.width, frame.height), QVideoFrameFormat::Format_YUV420P);
    QVideoFrame out(format);
    if (!out.map(QVideoFrame::WriteOnly)) {
        return QVideoFrame();
    }
    const int chromaW = (frame.width + 1) / 2;
    const int chromaH = (frame.height + 1) / 2;
    const unsigned char* const src = frame.i420.data();
    const unsigned char* const srcU = src + static_cast<std::size_t>(frame.width) * frame.height;
    const unsigned char* const srcV = srcU + static_cast<std::size_t>(chromaW) * chromaH;

    unsigned char* const dstY = out.bits(0);
    unsigned char* const dstU = out.bits(1);
    unsigned char* const dstV = out.bits(2);
    const int strideY = out.bytesPerLine(0);
    const int strideU = out.bytesPerLine(1);
    const int strideV = out.bytesPerLine(2);
    for (int y = 0; y < frame.height; ++y) {
        std::memcpy(dstY + static_cast<std::size_t>(y) * strideY,
            src + static_cast<std::size_t>(y) * frame.width, frame.width);
    }
    for (int y = 0; y < chromaH; ++y) {
        std::memcpy(dstU + static_cast<std::size_t>(y) * strideU,
            srcU + static_cast<std::size_t>(y) * chromaW, chromaW);
        std::memcpy(dstV + static_cast<std::size_t>(y) * strideV,
            srcV + static_cast<std::size_t>(y) * chromaW, chromaW);
    }
    out.unmap();
    return out;
}

}  // namespace

VideoPresenter::VideoPresenter(QObject* parent)
    : QObject(parent)
{
}

void VideoPresenter::setVideoSink(QVideoSink* sink)
{
    if (sink_ == sink) {
        return;
    }
    sink_ = sink;
    emit videoSinkChanged();
}

void VideoPresenter::present(const QVideoFrame& frame)
{
    if (sink_ != nullptr) {
        sink_->setVideoFrame(frame);
    }
}

QtVideoSource::QtVideoSource(VideoPresenter* localPreview)
    : session_(std::make_unique<QMediaCaptureSession>())
    , camera_(std::make_unique<QCamera>(QMediaDevices::defaultVideoInput()))
    , sink_(std::make_unique<QVideoSink>())
    , localPreview_(localPreview)
    , lastAccepted_(std::chrono::steady_clock::now())
{
    session_->setCamera(camera_.get());
    session_->setVideoSink(sink_.get());
    // The QVideoSink is the connection context, so the handler runs on this
    // (worker) thread's event loop and is dropped when the sink is destroyed.
    QObject::connect(sink_.get(), &QVideoSink::videoFrameChanged, sink_.get(),
        [this](const QVideoFrame& frame) { onFrame(frame); });
}

QtVideoSource::~QtVideoSource()
{
    stop();
}

void QtVideoSource::start()
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        running_ = true;
    }
    camera_->start();
}

void QtVideoSource::stop()
{
    camera_->stop();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
}

void QtVideoSource::onFrame(const QVideoFrame& frame)
{
    QImage image = frame.toImage();
    if (image.isNull()) {
        return;
    }
    // Mirror the raw camera frame to the local preview every frame for a smooth
    // self-view, independent of the (throttled) encode path.
    if (localPreview_ != nullptr) {
        QMetaObject::invokeMethod(
            localPreview_, [presenter = localPreview_, frame] { presenter->present(frame); },
            Qt::QueuedConnection);
    }
    // Throttle the encode path to the call frame rate even if the camera is faster.
    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(1000 / kCallVideoFps);
    if (now - lastAccepted_ < interval) {
        return;
    }
    lastAccepted_ = now;
    image = image.convertToFormat(QImage::Format_RGB888)
                .scaled(kCallVideoWidth, kCallVideoHeight, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
    VideoFrame converted = imageToI420(image);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::move(converted);
        fresh_ = true;
    }
    cv_.notify_one();
}

VideoFrame QtVideoSource::readFrame()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(
        lock, std::chrono::milliseconds(100), [this] { return fresh_ || !running_; });
    if (!fresh_ || !running_) {
        return VideoFrame{};
    }
    fresh_ = false;
    return latest_;
}

QtVideoSink::QtVideoSink(VideoPresenter* presenter)
    : presenter_(presenter)
{
}

void QtVideoSink::start()
{
}

void QtVideoSink::stop()
{
}

void QtVideoSink::writeFrame(const VideoFrame& frame)
{
    if (presenter_ == nullptr) {
        return;
    }
    const QVideoFrame qframe = i420ToQVideoFrame(frame);
    if (!qframe.isValid()) {
        return;
    }
    QMetaObject::invokeMethod(
        presenter_, [presenter = presenter_, qframe] { presenter->present(qframe); },
        Qt::QueuedConnection);
}

}  // namespace bazarish::app
