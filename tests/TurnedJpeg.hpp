// Bazarish project (c) 2026
#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QImage>
#include <QPainter>

namespace bazarish::test {

constexpr int kTurnedWide = 400;
constexpr int kTurnedHigh = 200;

// A camera's JPEG: pixels as the sensor saw them, the left half red, the turn left to the viewer.
inline QByteArray turnedLeftJpeg()
{
    constexpr int kStartOfImageBytes = 2;
    constexpr quint16 kApp1 = 0xFFE1;
    constexpr quint16 kSegmentLengthBytes = 2;
    constexpr char kExifHeader[] = {'E', 'x', 'i', 'f', '\0', '\0'};
    // TIFF 6.0: "MM" for big-endian, the magic 42, IFD0 right after the header.
    constexpr quint16 kBigEndianOrder = 0x4D4D;
    constexpr quint16 kTiffMagic = 42;
    constexpr quint32 kFirstIfdOffset = 8;
    // Exif 2.32, 4.6.4: Orientation is a SHORT; 6 means the stored picture is turned 90 degrees left.
    constexpr quint16 kOrientationTag = 0x0112;
    constexpr quint16 kShortType = 3;
    constexpr quint16 kTurnedLeft = 6;

    QImage image(kTurnedWide, kTurnedHigh, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QPainter painter(&image);
    painter.fillRect(0, 0, kTurnedWide / 2, kTurnedHigh, Qt::red);
    painter.end();
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG")) {
        return {};
    }

    QByteArray tiff;
    QDataStream out(&tiff, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << kBigEndianOrder << kTiffMagic << kFirstIfdOffset;
    out << quint16(1) << kOrientationTag << kShortType << quint32(1) << kTurnedLeft << quint16(0);
    out << quint32(0);
    const QByteArray payload = QByteArray(kExifHeader, sizeof(kExifHeader)) + tiff;

    QByteArray segment;
    QDataStream head(&segment, QIODevice::WriteOnly);
    head.setByteOrder(QDataStream::BigEndian);
    head << kApp1 << quint16(payload.size() + kSegmentLengthBytes);
    return jpeg.left(kStartOfImageBytes) + segment + payload + jpeg.mid(kStartOfImageBytes);
}

}  // namespace bazarish::test
