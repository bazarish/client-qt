// Bazarish project (c) 2026
#include "Identicon.hpp"

#include "AvatarStore.hpp"

#include <qrencode.h>

#include <algorithm>

#include <QColor>
#include <QCryptographicHash>
#include <QPainter>
#include <QUrl>

namespace bazarish::app {

QImage renderIdenticon(const QString& id, const int dim)
{
    const QByteArray hash
        = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256);
    const int hue = static_cast<unsigned char>(hash[0]) * 360 / 256;
    const QColor fg = QColor::fromHsl(hue, 160, 120);
    const QColor bg = QColor::fromHsl(hue, 40, 240);

    QImage image(dim, dim, QImage::Format_ARGB32);
    image.fill(bg);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fg);

    const int cells = 5;
    const double cell = static_cast<double>(dim) / cells;
    for (int row = 0; row < cells; ++row) {
        for (int col = 0; col < (cells + 1) / 2; ++col) {
            const int bit = row * 3 + col;
            if ((static_cast<unsigned char>(hash[1 + bit]) & 1) == 0) {
                continue;
            }
            const QRectF left(col * cell, row * cell, cell + 1, cell + 1);
            const QRectF right((cells - 1 - col) * cell, row * cell, cell + 1, cell + 1);
            painter.drawRect(left);
            painter.drawRect(right);
        }
    }
    painter.end();
    return image;
}

IdenticonProvider::IdenticonProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
{
}

QImage IdenticonProvider::requestImage(
    const QString& id, QSize* size, const QSize& requestedSize)
{
    const int dim = requestedSize.width() > 0 ? requestedSize.width() : 96;
    if (size != nullptr) {
        *size = QSize(dim, dim);
    }
    return renderIdenticon(id, dim);
}

AvatarProvider::AvatarProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
{
}

QImage AvatarProvider::requestImage(
    const QString& id, QSize* size, const QSize& requestedSize)
{
    const int dim = requestedSize.width() > 0 ? requestedSize.width() : 96;
    const QString fingerprint = id.section('?', 0, 0);

    const QImage stored = AvatarStore::instance().image(fingerprint);
    if (!stored.isNull()) {
        const int side = std::min(stored.width(), stored.height());
        const QImage square = stored.copy(
            (stored.width() - side) / 2, (stored.height() - side) / 2, side, side);
        const QImage scaled
            = square.scaled(dim, dim, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (size != nullptr) {
            *size = QSize(dim, dim);
        }
        return scaled;
    }

    if (size != nullptr) {
        *size = QSize(dim, dim);
    }
    return renderIdenticon(fingerprint, dim);
}

QrImageProvider::QrImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage QrImageProvider::requestImage(
    const QString& id, QSize* size, const QSize& requestedSize)
{
    const int dim = requestedSize.width() > 0 ? requestedSize.width() : 320;
    QImage image(dim, dim, QImage::Format_RGB32);
    image.fill(Qt::white);

    const QString text = QUrl::fromPercentEncoding(id.toUtf8());
    const QByteArray utf8 = text.toUtf8();
    QRcode* const qr = QRcode_encodeString8bit(utf8.constData(), 0, QR_ECLEVEL_M);
    if (qr != nullptr) {
        const int quiet = 2;
        const int span = qr->width + 2 * quiet;
        const double cell = static_cast<double>(dim) / span;
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        for (int y = 0; y < qr->width; ++y) {
            for (int x = 0; x < qr->width; ++x) {
                if (qr->data[y * qr->width + x] & 1) {
                    painter.drawRect(QRectF((x + quiet) * cell, (y + quiet) * cell,
                        cell + 1, cell + 1));
                }
            }
        }
        painter.end();
        QRcode_free(qr);
    }
    if (size != nullptr) {
        *size = QSize(dim, dim);
    }
    return image;
}

}  // namespace bazarish::app
