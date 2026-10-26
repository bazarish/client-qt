// Bazarish project (c) 2026
#include "AppFont.hpp"
#include "EmojiText.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QImage>
#include <QJSEngine>
#include <QJSValue>
#include <QPainter>
#include <QRawFont>
#include <QSet>
#include <QTextLayout>

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

namespace {

const QString kEmojiFamily = QStringLiteral("Twemoji Mozilla");
const QString kLibraryPragma = QStringLiteral(".pragma library");
constexpr int kPictureSide = 64;
constexpr int kPictureFontPixels = 48;

QJSValue pickerGroups(QJSEngine& engine)
{
    QFile source(QStringLiteral(BAZARISH_EMOJI_JS));
    CHECK(source.open(QIODevice::ReadOnly));
    QString script = QString::fromUtf8(source.readAll());
    script.replace(kLibraryPragma, QString());
    const QJSValue groups = engine.evaluate(script + QStringLiteral("\nkGroups"));
    CHECK(!groups.isError());
    CHECK(groups.isArray());
    return groups;
}

// One glyph with an advance, from the emoji font: a sequence it cannot join shows its parts.
bool drawnAsOneEmoji(const QString& emoji)
{
    QTextLayout layout(emoji, QGuiApplication::font());
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    const QList<QGlyphRun> runs = layout.glyphRuns();
    if (runs.size() != 1 || runs[0].rawFont().familyName() != kEmojiFamily) {
        return false;
    }
    const QList<quint32> glyphs = runs[0].glyphIndexes();
    const QList<QPointF> advances = runs[0].rawFont().advancesForGlyphIndexes(glyphs);
    int shown = 0;
    for (qsizetype i = 0; i < glyphs.size(); ++i) {
        if (glyphs[i] == 0) {
            return false;
        }
        if (advances[i].x() > 0) {
            ++shown;
        }
    }
    return shown == 1;
}

// Two entries the font draws alike, such as a territory under its country's flag, are one choice.
QByteArray pictureOf(const QString& emoji)
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(kPictureFontPixels);
    QImage image(kPictureSide, kPictureSide, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, emoji);
    painter.end();
    return QCryptographicHash::hash(
        QByteArrayView(image.constBits(), image.sizeInBytes()), QCryptographicHash::Sha1);
}

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    bazarish::app::installAppFont();

    CHECK(!drawnAsOneEmoji(QStringLiteral("\U0001FAE8")));
    CHECK(!drawnAsOneEmoji(QStringLiteral("\U0001F64D‍♂️")));

    QJSEngine engine;
    const QJSValue groups = pickerGroups(engine);
    const int groupCount = groups.property(QStringLiteral("length")).toInt();
    CHECK(groupCount > 0);
    int listed = 0;
    QSet<QByteArray> pictures;
    for (int g = 0; g < groupCount; ++g) {
        const QJSValue group = groups.property(g);
        const int size = group.property(QStringLiteral("length")).toInt();
        CHECK(size > 0);
        for (int i = 0; i < size; ++i) {
            const QString emoji = group.property(i).toString();
            if (!drawnAsOneEmoji(emoji) || bazarish::app::emojiOnlyCount(emoji) != 1) {
                std::fprintf(stderr, "not drawn as one emoji: %s\n", emoji.toUtf8().constData());
                return 1;
            }
            const QByteArray picture = pictureOf(emoji);
            if (pictures.contains(picture)) {
                std::fprintf(stderr, "drawn like another listed emoji: %s\n",
                    emoji.toUtf8().constData());
                return 1;
            }
            pictures.insert(picture);
            ++listed;
        }
    }
    std::printf("TestEmojiSet: %d emoji in %d groups, each drawn apart by the emoji font\n", listed,
        groupCount);
    return 0;
}
