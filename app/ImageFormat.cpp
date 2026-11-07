// Bazarish project (c) 2026
#include "ImageFormat.hpp"

namespace bazarish::app {

namespace {

// ISO/IEC 15948 file signature.
constexpr char kPngSignatureHex[] = "89504E470D0A1A0A";
// JPEG start-of-image marker followed by the next marker's lead byte.
constexpr char kJpegSignatureHex[] = "FFD8FF";

}  // namespace

QByteArray pictureFormat(const QByteArray& bytes)
{
    static const QByteArray kPng = QByteArray::fromHex(kPngSignatureHex);
    static const QByteArray kJpeg = QByteArray::fromHex(kJpegSignatureHex);
    if (bytes.startsWith(kPng)) {
        return QByteArrayLiteral("png");
    }
    if (bytes.startsWith(kJpeg)) {
        return QByteArrayLiteral("jpeg");
    }
    return {};
}

}  // namespace bazarish::app
