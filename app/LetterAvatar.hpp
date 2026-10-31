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

}  // namespace bazarish::app
