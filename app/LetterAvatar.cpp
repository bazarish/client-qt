// Bazarish project (c) 2026
#include "LetterAvatar.hpp"

#include <QCryptographicHash>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <cmath>

namespace bazarish::app {

namespace {

const QColor kLightInk(0xe6, 0xe6, 0xe6);
const QColor kDarkInk(0x1c, 0x1c, 0x1c);

// WCAG 2.2, "relative luminance" and "contrast ratio".
constexpr double kLinearBreak = 0.04045;
constexpr double kLinearSlope = 12.92;
constexpr double kGammaOffset = 0.055;
constexpr double kGammaScale = 1.055;
constexpr double kGamma = 2.4;
constexpr double kRedWeight = 0.2126;
constexpr double kGreenWeight = 0.7152;
constexpr double kBlueWeight = 0.0722;
constexpr double kFlare = 0.05;

double linear(const double channel)
{
    return channel <= kLinearBreak ? channel / kLinearSlope
                                   : std::pow((channel + kGammaOffset) / kGammaScale, kGamma);
}

double luminance(const QColor& color)
{
    return kRedWeight * linear(color.redF()) + kGreenWeight * linear(color.greenF())
        + kBlueWeight * linear(color.blueF());
}

}  // namespace

const std::array<QColor, kLetterPaletteSize>& letterPalette()
{
    static const std::array<QColor, kLetterPaletteSize> kPalette{
        QColor(0x8d, 0x45, 0x3a),
        QColor(0x70, 0x58, 0x2e),
        QColor(0x50, 0x62, 0x28),
        QColor(0x29, 0x65, 0x47),
        QColor(0x2c, 0x62, 0x6d),
        QColor(0x3a, 0x5c, 0x8d),
        QColor(0x51, 0x3b, 0x91),
        QColor(0x8d, 0x3a, 0x8d),
    };
    return kPalette;
}

QString letterOf(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, trimmed);
    const qsizetype end = graphemes.toNextBoundary();
    return trimmed.left(end > 0 ? end : 1).toUpper();
}

double contrastRatio(const QColor& first, const QColor& second)
{
    const double one = luminance(first);
    const double two = luminance(second);
    return (std::max(one, two) + kFlare) / (std::min(one, two) + kFlare);
}

QColor letterInk(const QColor& fill)
{
    return contrastRatio(fill, kLightInk) >= contrastRatio(fill, kDarkInk) ? kLightInk : kDarkInk;
}

QColor letterFill(const QString& name)
{
    const QByteArray digest = QCryptographicHash::hash(
        name.normalized(QString::NormalizationForm_C).toUtf8(), QCryptographicHash::Sha256);
    const auto first = static_cast<unsigned char>(digest.at(0));
    return letterPalette()[first % kLetterPaletteSize];
}

}  // namespace bazarish::app
