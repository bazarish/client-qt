// Bazarish project (c) 2026
#include "Identicon.hpp"

#include <qrencode.h>

#include <QColor>
#include <QCryptographicHash>
#include <QPainter>
#include <QUrl>

namespace bazarish::app {

IdenticonProvider::IdenticonProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage IdenticonProvider::requestImage(
    const QString& id, QSize* size, const QSize& requestedSize)
{
    const int dim = requestedSize.width() > 0 ? requestedSize.width() : 96;
    if (size != nullptr) {
        *size = QSize(dim, dim);
    }

    // Derive a stable hash; first bytes pick the hue, the rest fill a 5x5
    // mirrored grid (GitHub-identicon style).
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
            const int bit = row * 3 + col;  // 5 rows x 3 unique cols = 15 bits
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

    // The caller percent-encodes the payload (encodeURIComponent) so characters
    // like '&', '/', '?' survive the image:// URL; decode it back here so the QR
    // carries the clean link (e.g. bazarish://invite?...&...), not the escaped
    // form. Decoding is a no-op for payloads that have no percent escapes.
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
