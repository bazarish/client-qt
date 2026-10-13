// Bazarish project (c) 2026
#include "LetterAvatar.hpp"

#include <QColor>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
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

constexpr double kTextContrast = 4.5;
constexpr double kLinearThreshold = 0.03928;
constexpr double kLinearDivisor = 12.92;
constexpr double kGammaOffset = 0.055;
constexpr double kGammaDivisor = 1.055;
constexpr double kGamma = 2.4;
constexpr double kRedWeight = 0.2126;
constexpr double kGreenWeight = 0.7152;
constexpr double kBlueWeight = 0.0722;
constexpr double kFlare = 0.05;

double channel(const double value)
{
    return value <= kLinearThreshold ? value / kLinearDivisor
                                     : std::pow((value + kGammaOffset) / kGammaDivisor, kGamma);
}

double luminance(const QColor& color)
{
    return kRedWeight * channel(color.redF()) + kGreenWeight * channel(color.greenF())
        + kBlueWeight * channel(color.blueF());
}

double contrast(const QColor& one, const QColor& other)
{
    const double a = luminance(one);
    const double b = luminance(other);
    return (std::max(a, b) + kFlare) / (std::min(a, b) + kFlare);
}

}  // namespace

int main()
{
    QSet<QRgb> distinct;
    for (const QColor& fill : letterPalette()) {
        CHECK(contrast(letterInk(), fill) >= kTextContrast);
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
    std::printf("TestLetterAvatar passed\n");
    return 0;
}
