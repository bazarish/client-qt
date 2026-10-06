// Bazarish project (c) 2026
#include "Picture.hpp"

#include <QBuffer>

#include <algorithm>

namespace bazarish::app {

namespace {

constexpr int kMaxImageEdge = 1600;
constexpr qint64 kMaxImageBytes = 256 * 1024;
constexpr int kJpegQuality = 85;
constexpr int kQualityStep = 10;
constexpr int kMinJpegQuality = 45;
constexpr double kEdgeStep = 0.75;
constexpr int kMinImageEdge = 640;

QByteArray encodedImage(QImage image, QString* const format)
{
    if (image.width() > kMaxImageEdge || image.height() > kMaxImageEdge) {
        image = image.scaled(kMaxImageEdge, kMaxImageEdge, Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    const bool transparent = image.hasAlphaChannel();
    *format = transparent ? QStringLiteral("png") : QStringLiteral("jpg");
    int quality = kJpegQuality;
    for (;;) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly)) {
            return {};
        }
        if (!image.save(&buffer, transparent ? "PNG" : "JPEG", transparent ? -1 : quality)) {
            return {};
        }
        if (bytes.size() <= kMaxImageBytes) {
            return bytes;
        }
        if (!transparent && quality > kMinJpegQuality) {
            quality -= kQualityStep;
            continue;
        }
        const int edge = static_cast<int>(std::max(image.width(), image.height()) * kEdgeStep);
        if (edge < kMinImageEdge) {
            return bytes;
        }
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
}

}  // namespace

PreparedPicture preparePicture(const QImage& image, const QString& baseName)
{
    if (image.isNull()) {
        return {};
    }
    QString format;
    PreparedPicture prepared;
    prepared.bytes = encodedImage(image, &format);
    if (prepared.bytes.isEmpty()) {
        return {};
    }
    prepared.name = (baseName.isEmpty() ? QStringLiteral("image") : baseName) + "." + format;
    prepared.mime = format == QStringLiteral("png") ? QStringLiteral("image/png")
                                                    : QStringLiteral("image/jpeg");
    return prepared;
}

}  // namespace bazarish::app
