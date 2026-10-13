// Bazarish project (c) 2026
#include "LetterAvatar.hpp"

#include <QCryptographicHash>
#include <QTextBoundaryFinder>

namespace bazarish::app {

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

QColor letterInk()
{
    return QColor(0xd7, 0xdb, 0xd8);
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

QColor letterFill(const QString& name)
{
    const QByteArray digest = QCryptographicHash::hash(
        name.normalized(QString::NormalizationForm_C).toUtf8(), QCryptographicHash::Sha256);
    const auto first = static_cast<unsigned char>(digest.at(0));
    return letterPalette()[first % kLetterPaletteSize];
}

}  // namespace bazarish::app
