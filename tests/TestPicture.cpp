// Bazarish project (c) 2026
#include "Picture.hpp"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

namespace {

constexpr int kWide = 400;
constexpr int kHigh = 200;
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
constexpr int kRedLeast = 200;

// The camera's way: pixels as the sensor saw them, the turn left to the viewer.
QByteArray turnedLeftJpeg()
{
    QImage image(kWide, kHigh, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QPainter painter(&image);
    painter.fillRect(0, 0, kWide / 2, kHigh, Qt::red);
    painter.end();
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    CHECK(buffer.open(QIODevice::WriteOnly));
    CHECK(image.save(&buffer, "JPEG"));

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

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir folder;
    CHECK(folder.isValid());
    const QString path = folder.filePath(QStringLiteral("camera.jpg"));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    const QByteArray original = turnedLeftJpeg();
    CHECK(file.write(original) == original.size());
    file.close();
    CHECK(QImage(path).width() == kWide);

    const bazarish::app::PreparedPicture picture = bazarish::app::preparePictureFile(path);
    CHECK(!picture.isEmpty());
    CHECK(picture.name == QStringLiteral("camera.jpg"));
    QImage sent;
    CHECK(sent.loadFromData(picture.bytes));
    CHECK(sent.width() == kHigh && sent.height() == kWide);
    CHECK(qRed(sent.pixel(kHigh / 2, kWide / 4)) > kRedLeast);
    CHECK(qRed(sent.pixel(kHigh / 2, kWide * 3 / 4)) < kRedLeast);

    std::printf("TestPicture: all checks passed\n");
    return 0;
}
