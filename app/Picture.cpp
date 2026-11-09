// Bazarish project (c) 2026
#include "Picture.hpp"

#include <bazarish/Limits.hpp>

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>

namespace bazarish::app {

namespace {

constexpr int kMaxImageEdge = 1600;
constexpr int kJpegQuality = 85;
constexpr int kQualityStep = 10;
constexpr int kMinJpegQuality = 45;
constexpr double kEdgeStep = 0.75;
constexpr int kMinImageEdge = 640;

struct Encoded {
    QByteArray bytes;
    QString format;
};

Encoded encodedImage(QImage image)
{
    if (image.width() > kMaxImageEdge || image.height() > kMaxImageEdge) {
        image = image.scaled(kMaxImageEdge, kMaxImageEdge, Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    const bool transparent = image.hasAlphaChannel();
    const QString format = transparent ? QStringLiteral("png") : QStringLiteral("jpg");
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
        if (bytes.size() <= static_cast<qsizetype>(bazarish::kMaxPictureBytes)) {
            return {bytes, format};
        }
        if (!transparent && quality > kMinJpegQuality) {
            quality -= kQualityStep;
            continue;
        }
        const int edge = static_cast<int>(std::max(image.width(), image.height()) * kEdgeStep);
        if (edge < kMinImageEdge) {
            return {bytes, format};
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
    const Encoded encoded = encodedImage(image);
    const QString& format = encoded.format;
    PreparedPicture prepared;
    prepared.bytes = encoded.bytes;
    if (prepared.bytes.isEmpty()) {
        return {};
    }
    const QString extension = QStringLiteral(".") + format;
    QString stem = baseName.isEmpty() ? QStringLiteral("image") : baseName;
    while ((stem + extension).toUtf8().size() > static_cast<qsizetype>(bazarish::kMaxMediaNameBytes)) {
        stem.chop(stem.size() >= 2 && stem.at(stem.size() - 1).isLowSurrogate() ? 2 : 1);
    }
    prepared.name = stem + extension;
    prepared.mime = format == QStringLiteral("png") ? QStringLiteral("image/png")
                                                    : QStringLiteral("image/jpeg");
    return prepared;
}

PreparedPicture preparePictureFile(const QString& location)
{
    QImageReader reader(location);
    reader.setAutoTransform(true);
    return preparePicture(reader.read(), QFileInfo(location).completeBaseName());
}

}  // namespace bazarish::app
