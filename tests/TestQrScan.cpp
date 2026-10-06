// Bazarish project (c) 2026
#include "QrScanner.hpp"

#include <bazarish/Descriptor.hpp>

#include <qrencode.h>

#include <QImage>
#include <QString>
#include <QVariant>
#include <QVideoSink>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

namespace {

constexpr unsigned char kWhite = 0xff;
constexpr unsigned char kBlack = 0x00;
// ISO/IEC 18004 asks for four light modules around a symbol.
constexpr int kQuietModules = 4;
constexpr int kCloseModulePixels = 6;
// Wide enough that the scanner has to scale the still down before reading it.
constexpr int kFarModulePixels = 16;

QImage renderQr(const std::string& payload, const int modulePixels)
{
    QRcode* const qr = QRcode_encodeString8bit(payload.c_str(), 0, QR_ECLEVEL_M);
    CHECK(qr != nullptr);
    const int span = (qr->width + 2 * kQuietModules) * modulePixels;
    QImage image(span, span, QImage::Format_Grayscale8);
    image.fill(kWhite);
    for (int y = 0; y < qr->width; ++y) {
        for (int x = 0; x < qr->width; ++x) {
            if ((qr->data[y * qr->width + x] & 1) == 0) {
                continue;
            }
            for (int row = 0; row < modulePixels; ++row) {
                unsigned char* const line
                    = image.scanLine((y + kQuietModules) * modulePixels + row);
                std::memset(line + (x + kQuietModules) * modulePixels, kBlack, modulePixels);
            }
        }
    }
    QRcode_free(qr);
    return image;
}

}  // namespace

int main()
{
    const bazarish::Descriptor descriptor{
        "dlkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq",
        "dlkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq.b32.i2p",
        "0123456789abcdef0123456789abcdef", "anna"};
    const std::string uri = bazarish::encodeDescriptor(descriptor);
    const QString expected = QString::fromStdString(uri);

    bazarish::app::QrScanner scanner;
    CHECK(scanner.read(renderQr(uri, kCloseModulePixels)) == expected);
    CHECK(scanner.read(renderQr(uri, kFarModulePixels)) == expected);

    QImage blank(320, 320, QImage::Format_Grayscale8);
    blank.fill(kWhite);
    CHECK(scanner.read(blank).isEmpty());
    CHECK(scanner.read(QImage()).isEmpty());

    QVideoSink videoOutputSink;
    CHECK(scanner.setProperty("sink", QVariant::fromValue(&videoOutputSink)));
    CHECK(scanner.sink() == &videoOutputSink);

    std::printf("TestQrScan ok\n");
    return 0;
}
