// Bazarish project (c) 2026
#include "EmojiText.hpp"

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

using bazarish::app::emojiOnlyCount;

int main()
{
    CHECK(emojiOnlyCount(QStringLiteral("😀")) == 1);
    CHECK(emojiOnlyCount(QStringLiteral("❤️")) == 1);
    CHECK(emojiOnlyCount(QStringLiteral("👍🏽")) == 1);
    CHECK(emojiOnlyCount(QStringLiteral("👨‍👩‍👧")) == 1);
    CHECK(emojiOnlyCount(QStringLiteral("🏳️‍🌈")) == 1);
    CHECK(emojiOnlyCount(QStringLiteral("🇷🇺")) == 1);
    CHECK(emojiOnlyCount(QStringLiteral("1️⃣")) == 1);
    CHECK(emojiOnlyCount(QStringLiteral("😀😂🔥")) == 3);
    CHECK(emojiOnlyCount(QStringLiteral(" 😀  🎉\n")) == 2);

    CHECK(emojiOnlyCount(QString()) == 0);
    CHECK(emojiOnlyCount(QStringLiteral("   ")) == 0);
    CHECK(emojiOnlyCount(QStringLiteral("ok 👍")) == 0);
    CHECK(emojiOnlyCount(QStringLiteral("1")) == 0);
    CHECK(emojiOnlyCount(QStringLiteral("🇷")) == 0);
    CHECK(emojiOnlyCount(QStringLiteral("*😀*")) == 0);

    std::printf("TestEmojiText: all checks passed\n");
    return 0;
}
