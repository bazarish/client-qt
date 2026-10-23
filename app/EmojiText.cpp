// Bazarish project (c) 2026
#include "EmojiText.hpp"

#include <QChar>
#include <QList>

namespace bazarish::app {

namespace {

constexpr char32_t kZeroWidthJoiner = 0x200D;
constexpr char32_t kKeycap = 0x20E3;
constexpr char32_t kTextStyle = 0xFE0E;
constexpr char32_t kEmojiStyle = 0xFE0F;
constexpr char32_t kSkinToneFirst = 0x1F3FB;
constexpr char32_t kSkinToneLast = 0x1F3FF;
constexpr char32_t kRegionalFirst = 0x1F1E6;
constexpr char32_t kRegionalLast = 0x1F1FF;
constexpr char32_t kTagFirst = 0xE0020;
constexpr char32_t kTagLast = 0xE007F;

struct Range {
    char32_t first;
    char32_t last;
};

// Unicode's Extended_Pictographic, in the blocks emoji actually come from.
constexpr Range kPictographic[] = {
    {0x00A9, 0x00A9}, {0x00AE, 0x00AE}, {0x203C, 0x203C}, {0x2049, 0x2049},
    {0x2122, 0x2122}, {0x2139, 0x2139}, {0x2194, 0x21AA}, {0x231A, 0x23FF},
    {0x24C2, 0x24C2}, {0x25AA, 0x25FE}, {0x2600, 0x27BF}, {0x2934, 0x2935},
    {0x2B05, 0x2B55}, {0x3030, 0x3030}, {0x303D, 0x303D}, {0x3297, 0x3299},
    {0x1F000, 0x1F1E5}, {0x1F200, 0x1FAFF},
};

bool pictographic(const char32_t c)
{
    for (const Range& range : kPictographic) {
        if (c >= range.first && c <= range.last) {
            return true;
        }
    }
    return false;
}

bool regional(const char32_t c) { return c >= kRegionalFirst && c <= kRegionalLast; }

bool keycapBase(const char32_t c) { return (c >= '0' && c <= '9') || c == '#' || c == '*'; }

// What only ever follows an emoji and changes how it looks, never how many there are.
bool modifier(const char32_t c)
{
    return c == kEmojiStyle || c == kTextStyle || c == kKeycap
        || (c >= kSkinToneFirst && c <= kSkinToneLast) || (c >= kTagFirst && c <= kTagLast);
}

}  // namespace

int emojiOnlyCount(const QString& text)
{
    const QList<uint> points = text.toUcs4();
    int count = 0;
    qsizetype at = 0;
    while (at < points.size()) {
        const char32_t c = points[at];
        if (QChar::isSpace(c)) {
            ++at;
            continue;
        }
        if (regional(c)) {
            if (at + 1 >= points.size() || !regional(points[at + 1])) {
                return 0;
            }
            at += 2;
        } else if (keycapBase(c)) {
            qsizetype next = at + 1;
            if (next < points.size() && points[next] == kEmojiStyle) {
                ++next;
            }
            if (next >= points.size() || points[next] != kKeycap) {
                return 0;
            }
            at = next + 1;
        } else if (pictographic(c)) {
            ++at;
            for (;;) {
                while (at < points.size() && modifier(points[at])) {
                    ++at;
                }
                if (at + 1 < points.size() && points[at] == kZeroWidthJoiner
                    && pictographic(points[at + 1])) {
                    at += 2;
                    continue;
                }
                break;
            }
        } else {
            return 0;
        }
        ++count;
    }
    return count;
}

}  // namespace bazarish::app
