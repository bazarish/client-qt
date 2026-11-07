// Bazarish project (c) 2026
#include "AvatarStore.hpp"

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>

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

using namespace bazarish::app;

namespace {

constexpr int kAvatarEdge = 64;
constexpr int kOverEdge = AvatarStore::kMaxSide * 2;

QByteArray encoded(const int edge, const char* format)
{
    QImage image(edge, edge, QImage::Format_RGB32);
    image.fill(Qt::darkGreen);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    CHECK(buffer.open(QIODevice::WriteOnly));
    CHECK(image.save(&buffer, format));
    return bytes;
}

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    AvatarStore& store = AvatarStore::instance();

    store.put(QStringLiteral("png"), encoded(kAvatarEdge, "PNG"));
    CHECK(store.has(QStringLiteral("png")));
    CHECK(store.image(QStringLiteral("png")).width() == kAvatarEdge);

    store.put(QStringLiteral("jpeg"), encoded(kAvatarEdge, "JPEG"));
    CHECK(!store.image(QStringLiteral("jpeg")).isNull());

    store.put(QStringLiteral("svg"),
        QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
                          "<rect width=\"8\" height=\"8\"/></svg>"));
    CHECK(!store.has(QStringLiteral("svg")));
    CHECK(store.image(QStringLiteral("svg")).isNull());

    store.put(QStringLiteral("gif"), QByteArrayLiteral("GIF89a\x01\x00\x01\x00\x00\x00\x00;"));
    CHECK(!store.has(QStringLiteral("gif")));

    store.put(QStringLiteral("huge"), encoded(kOverEdge, "PNG"));
    CHECK(store.image(QStringLiteral("huge")).isNull());

    store.put(QStringLiteral("png"), QByteArray());
    CHECK(!store.has(QStringLiteral("png")));
    CHECK(store.image(QStringLiteral("png")).isNull());
    return 0;
}
