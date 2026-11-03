// Bazarish project (c) 2026
#pragma once

#include <QColor>
#include <QString>

#include <array>
#include <cstddef>

namespace bazarish::app {

inline constexpr std::size_t kLetterPaletteSize = 8;

const std::array<QColor, kLetterPaletteSize>& letterPalette();
QString letterOf(const QString& name);
QColor letterFill(const QString& name);
// The letter's colour on a fill: light grey or dark, whichever stands out more from it.
QColor letterInk(const QColor& fill);
double contrastRatio(const QColor& first, const QColor& second);

}  // namespace bazarish::app
