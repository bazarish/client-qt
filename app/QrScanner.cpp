// Bazarish project (c) 2026
#include "QrScanner.hpp"

#include <quirc.h>

#include <QImage>
#include <QVideoFrame>

#include <cstring>
#include <stdexcept>

namespace bazarish::app {

namespace {

// Measured: enough for an invite code held up to the lens, and quirc's cost
// grows with the area.
constexpr int kScanWidth = 800;

}  // namespace

QrScanner::QrScanner(QObject* parent)
    : QObject(parent)
    , decoder_(quirc_new())
{
    if (decoder_ == nullptr) {
        throw std::runtime_error("quirc_new failed");
    }
}

QrScanner::~QrScanner()
{
    quirc_destroy(decoder_);
}

void QrScanner::setSink(QVideoSink* const sink)
{
    if (sink_ == sink) {
        return;
    }
    if (sink_ != nullptr) {
        disconnect(sink_, &QVideoSink::videoFrameChanged, this, nullptr);
    }
    sink_ = sink;
    if (sink_ != nullptr) {
        connect(sink_, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame& frame) { readFrame(frame); });
    }
    emit sinkChanged();
}

void QrScanner::readFrame(const QVideoFrame& frame)
{
    if (!frame.isValid()) {
        return;
    }
    const QString text = read(frame.toImage());
    if (!text.isEmpty()) {
        emit decoded(text);
    }
}

QString QrScanner::read(const QImage& frame)
{
    if (frame.isNull()) {
        return {};
    }
    QImage image = frame.width() > kScanWidth
        ? frame.scaledToWidth(kScanWidth, Qt::SmoothTransformation)
        : frame;
    image = image.convertToFormat(QImage::Format_Grayscale8);
    if (image.isNull()) {
        return {};
    }
    if (image.width() != width_ || image.height() != height_) {
        if (quirc_resize(decoder_, image.width(), image.height()) < 0) {
            throw std::runtime_error("quirc_resize failed");
        }
        width_ = image.width();
        height_ = image.height();
    }

    int bufferWidth = 0;
    int bufferHeight = 0;
    unsigned char* const buffer = quirc_begin(decoder_, &bufferWidth, &bufferHeight);
    for (int y = 0; y < bufferHeight; ++y) {
        std::memcpy(buffer + static_cast<std::ptrdiff_t>(y) * bufferWidth,
            image.constScanLine(y), static_cast<std::size_t>(bufferWidth));
    }
    quirc_end(decoder_);

    const int found = quirc_count(decoder_);
    for (int i = 0; i < found; ++i) {
        quirc_code code;
        quirc_extract(decoder_, i, &code);
        quirc_data data;
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
            continue;
        }
        return QString::fromUtf8(
            reinterpret_cast<const char*>(data.payload), data.payload_len);
    }
    return {};
}

}  // namespace bazarish::app
