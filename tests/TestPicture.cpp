// Bazarish project (c) 2026
#include "Picture.hpp"
#include "TurnedJpeg.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
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

using bazarish::test::kTurnedHigh;
using bazarish::test::kTurnedWide;

namespace {

constexpr int kRedLeast = 200;

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir folder;
    CHECK(folder.isValid());
    const QString path = folder.filePath(QStringLiteral("camera.jpg"));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    const QByteArray original = bazarish::test::turnedLeftJpeg();
    CHECK(!original.isEmpty());
    CHECK(file.write(original) == original.size());
    file.close();
    CHECK(QImage(path).width() == kTurnedWide);

    const bazarish::app::PreparedPicture picture = bazarish::app::preparePictureFile(path);
    CHECK(!picture.isEmpty());
    CHECK(picture.name == QStringLiteral("camera.jpg"));
    QImage sent;
    CHECK(sent.loadFromData(picture.bytes));
    CHECK(sent.width() == kTurnedHigh && sent.height() == kTurnedWide);
    CHECK(qRed(sent.pixel(kTurnedHigh / 2, kTurnedWide / 4)) > kRedLeast);
    CHECK(qRed(sent.pixel(kTurnedHigh / 2, kTurnedWide * 3 / 4)) < kRedLeast);

    std::printf("TestPicture: all checks passed\n");
    return 0;
}
