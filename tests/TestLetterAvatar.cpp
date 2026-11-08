// Bazarish project (c) 2026
#include "LetterAvatar.hpp"
#include "TestUtil.hpp"

#include <QColor>
#include <QSet>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace bazarish::app;

int main()
{
    QSet<QRgb> distinct;
    for (const QColor& fill : letterPalette()) {
        distinct.insert(fill.rgb());
    }
    CHECK(static_cast<std::size_t>(distinct.size()) == kLetterPaletteSize);

    CHECK(letterOf(QStringLiteral("ann")) == QStringLiteral("A"));
    CHECK(letterOf(QStringLiteral("  ева")) == QStringLiteral("Е"));
    CHECK(letterOf(QStringLiteral("émile")) == QStringLiteral("É"));
    CHECK(letterOf(QString()).isEmpty());

    CHECK(letterFill(QStringLiteral("Ann")) == letterFill(QStringLiteral("Ann")));
    QSet<QRgb> used;
    for (const char* const name : {"Ann", "Bob", "Cid", "Dan", "Eve", "Fay", "Gus", "Hal",
             "Ivy", "Jon", "Kim", "Lou"}) {
        used.insert(letterFill(QString::fromLatin1(name)).rgb());
    }
    CHECK(used.size() > 1);

    // WCAG 2.2 AA for normal text.
    constexpr double kReadable = 4.5;
    for (const QColor& fill : letterPalette()) {
        CHECK(contrastRatio(fill, letterInk(fill)) >= kReadable);
    }
    CHECK(letterInk(QColor(Qt::white)).lightness() < letterInk(QColor(Qt::black)).lightness());
    std::printf("TestLetterAvatar passed\n");
    return 0;
}
