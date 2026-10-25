// Bazarish project (c) 2026
#include "PictureStore.hpp"
#include "TurnedJpeg.hpp"

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QRandomGenerator>

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

// Decoded at 4 bytes a pixel this is 16 MiB: a handful passes the decoded budget.
constexpr int kLargeEdge = 2048;
constexpr int kLargePictures = 6;
// Noise does not compress: four of these pass the budget for kept bytes.
constexpr int kNoisyEdge = 2048;
constexpr int kNoisyPictures = 4;

QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    CHECK(buffer.open(QIODevice::WriteOnly));
    CHECK(image.save(&buffer, "PNG"));
    return bytes;
}

QByteArray flat(const int edge, const QColor& color)
{
    QImage image(edge, edge, QImage::Format_RGB32);
    image.fill(color);
    return png(image);
}

QByteArray noise(const int edge)
{
    QImage image(edge, edge, QImage::Format_RGB32);
    for (int y = 0; y < edge; ++y) {
        auto* line = reinterpret_cast<quint32*>(image.scanLine(y));
        for (int x = 0; x < edge; ++x) {
            line[x] = QRandomGenerator::global()->generate();
        }
    }
    return png(image);
}

void decodedBudgetKeepsEveryPicture()
{
    PictureStore& store = PictureStore::instance();
    store.clear();
    for (int i = 0; i < kLargePictures; ++i) {
        const QString id = QStringLiteral("large%1").arg(i);
        CHECK(store.put(id, flat(kLargeEdge, QColor::fromHsv(i * 40, 200, 200))));
        CHECK(!store.image(id).isNull());
    }
    for (int i = 0; i < kLargePictures; ++i) {
        const QString id = QStringLiteral("large%1").arg(i);
        CHECK(store.has(id));
        CHECK(store.image(id).size() == QSize(kLargeEdge, kLargeEdge));
    }
}

void keptBytesBudgetLetsTheOldestGoAndRestoreBringsItBack()
{
    PictureStore& store = PictureStore::instance();
    store.clear();
    QByteArray first;
    for (int i = 0; i < kNoisyPictures; ++i) {
        const QByteArray bytes = noise(kNoisyEdge);
        if (i == 0) {
            first = bytes;
        }
        CHECK(store.put(QStringLiteral("noisy%1").arg(i), bytes));
    }
    CHECK(!store.has(QStringLiteral("noisy0")));
    CHECK(store.has(QStringLiteral("noisy%1").arg(kNoisyPictures - 1)));

    const int revision = store.revision();
    CHECK(store.restore(QStringLiteral("noisy0"), first));
    CHECK(store.has(QStringLiteral("noisy0")));
    CHECK(!store.image(QStringLiteral("noisy0")).isNull());
    CHECK(store.revision() == revision);
    CHECK(!store.restore(QStringLiteral("text"), QByteArrayLiteral("not a picture")));
}

}  // namespace

// What another client sent straight from its camera shows the way its gallery did.
void cameraOrientationIsApplied()
{
    PictureStore& store = PictureStore::instance();
    store.clear();
    const QString e2eId = QStringLiteral("turned");
    CHECK(store.restore(e2eId, bazarish::test::turnedLeftJpeg()));
    const QImage shown = store.image(e2eId);
    CHECK(shown.width() == bazarish::test::kTurnedHigh);
    CHECK(shown.height() == bazarish::test::kTurnedWide);
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    decodedBudgetKeepsEveryPicture();
    keptBytesBudgetLetsTheOldestGoAndRestoreBringsItBack();
    cameraOrientationIsApplied();
    std::printf("TestPictureStore: ok\n");
    return 0;
}
