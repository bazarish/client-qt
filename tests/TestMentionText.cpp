// Bazarish project (c) 2026
#include "MentionText.hpp"
#include "TestUtil.hpp"

#include <Mentions.hpp>

#include <QString>
#include <QStringList>

#include <cstdio>

using namespace bazarish::app;

namespace {

void testRangesAreQStringPositions()
{
    const QStringList members{QStringLiteral("Анна Мария"), QStringLiteral("Bob"),
        QString::fromUtf8("李雷")};
    const QString text = QString::fromUtf8("😀😀 (@Анна_Мария) @李雷… @Bob, @Bo x@Bob");
    const QVector<MentionRange> ranges = mentionRanges(text, members);
    CHECK(ranges.size() == 3);
    CHECK(text.mid(ranges[0].from, ranges[0].length) == QString::fromUtf8("@Анна_Мария"));
    CHECK(text.mid(ranges[1].from, ranges[1].length) == QString::fromUtf8("@李雷"));
    CHECK(text.mid(ranges[2].from, ranges[2].length) == QStringLiteral("@Bob"));
    // Two emoji are four UTF-16 units and eight UTF-8 bytes.
    CHECK(ranges[0].from == 6);
    CHECK(mentionRanges(text, {}).isEmpty());
}

void testTypedMention()
{
    CHECK(!mentionTypedAt(QStringLiteral("hello @")));
    CHECK(!mentionTypedAt(QStringLiteral("mail@an")));
    CHECK(!mentionTypedAt(QStringLiteral("@an na")));
    const auto first = mentionTypedAt(QStringLiteral("@a"));
    CHECK(first && first->from == 0 && first->typed == QStringLiteral("a"));
    const auto quoted = mentionTypedAt(QString::fromUtf8("🙂 «@Ан"));
    CHECK(quoted && quoted->from == 4 && quoted->typed == QString::fromUtf8("Ан"));
    const auto later = mentionTypedAt(QStringLiteral("@bob and @Ann_M"));
    CHECK(later && later->from == 9 && later->typed == QStringLiteral("Ann_M"));
}

void testChoices()
{
    const QStringList members{QStringLiteral("anna"), QStringLiteral("Anna Maria"),
        QStringLiteral("Annika"), QStringLiteral("Bob"), QStringLiteral("ANNE"),
        QStringLiteral("Annabel"), QStringLiteral("Annette"), QStringLiteral("Anouk")};
    const QStringList many = mentionChoices(members, QStringLiteral("Bob"), QStringLiteral("an"));
    CHECK(many.size() == static_cast<qsizetype>(bazarish::client::kMaxMentionCandidates));
    CHECK(!many.contains(QStringLiteral("Bob")));
    CHECK(mentionChoices(members, QStringLiteral("Bob"), QStringLiteral("bo")).isEmpty());
    CHECK(mentionChoices(members, {}, QStringLiteral("bo")) == QStringList{QStringLiteral("Bob")});
    CHECK(mentionChoices(members, {}, QStringLiteral("anna_m"))
        == QStringList{QStringLiteral("Anna Maria")});
    CHECK(mentionChoices({QString::fromUtf8("Ёжик")}, {}, QString::fromUtf8("ё"))
        == QStringList{QString::fromUtf8("Ёжик")});
    CHECK(mentionText(QStringLiteral("Anna Maria")) == QStringLiteral("@Anna_Maria"));

    const QVariantMap offer = mentionOffer(QStringLiteral("hi @bo"), members, {});
    CHECK(offer.value(QStringLiteral("from")).toInt() == 3);
    CHECK(offer.value(QStringLiteral("choices")).toStringList() == QStringList{QStringLiteral("Bob")});
    CHECK(mentionOffer(QStringLiteral("hi @bo"), members, QStringLiteral("Bob")).isEmpty());
    CHECK(mentionOffer(QStringLiteral("hi @bo"), {}, {}).isEmpty());
    CHECK(mentionOffer(QStringLiteral("hi bo"), members, {}).isEmpty());
}

void testListOrderAndSearch()
{
    const QStringList names{QString::fromUtf8("Жора"), QString::fromUtf8("ёлка"),
        QString::fromUtf8("Елена"), QStringLiteral("user10"), QStringLiteral("user9"),
        QStringLiteral("Zed"), QStringLiteral("alice")};
    const QStringList english = namesInOrder(names, QStringLiteral("en"));
    CHECK(english.indexOf(QStringLiteral("alice")) < english.indexOf(QStringLiteral("Zed")));
    CHECK(english.indexOf(QStringLiteral("user9")) < english.indexOf(QStringLiteral("user10")));
    const QStringList russian = namesInOrder(names, QStringLiteral("ru"));
    CHECK(russian.indexOf(QString::fromUtf8("Елена")) < russian.indexOf(QString::fromUtf8("ёлка")));
    CHECK(russian.indexOf(QString::fromUtf8("ёлка")) < russian.indexOf(QString::fromUtf8("Жора")));

    CHECK(namesMatching(names, QStringLiteral("  ")) == names);
    CHECK(namesMatching(names, QStringLiteral("USER")).size() == 2);
    CHECK(namesMatching(names, QString::fromUtf8("ЛЕН")) == QStringList{QString::fromUtf8("Елена")});
    CHECK(namesMatching({QStringLiteral("Anna Maria")}, QStringLiteral("@anna_m")).size() == 1);
    CHECK(namesMatching(names, QStringLiteral("nobody")).isEmpty());
}

}  // namespace

int main()
try {
    testRangesAreQStringPositions();
    testTypedMention();
    testChoices();
    testListOrderAndSearch();
    std::fprintf(stderr, "TestMentionText passed\n");
    return 0;
} catch (...) {
    return testutil::failed();
}
