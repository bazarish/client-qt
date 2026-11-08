// Bazarish project (c) 2026
#include "EmojiText.hpp"
#include "TestUtil.hpp"

#include <cstdio>
#include <cstdlib>

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
