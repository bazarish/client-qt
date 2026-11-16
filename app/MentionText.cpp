// Bazarish project (c) 2026
#include "MentionText.hpp"

#include <Mentions.hpp>

#include <QCollator>
#include <QLocale>

#include <algorithm>
#include <string>
#include <vector>

namespace bazarish::app {

namespace {

constexpr QChar kMentionSign(u'@');

std::vector<std::string> utf8Names(const QStringList& names)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(names.size()));
    for (const QString& name : names) {
        out.push_back(name.toStdString());
    }
    return out;
}

int utf16Length(const std::string& utf8, const std::size_t bytes)
{
    return static_cast<int>(QString::fromUtf8(utf8.data(), static_cast<qsizetype>(bytes)).size());
}

}  // namespace

QVector<MentionRange> mentionRanges(const QString& text, const QStringList& names)
{
    QVector<MentionRange> ranges;
    if (names.isEmpty()) {
        return ranges;
    }
    const std::string utf8 = text.toStdString();
    for (const client::MentionSpan& span : client::findMentions(utf8, utf8Names(names))) {
        const int from = utf16Length(utf8, span.offset);
        ranges.push_back({from, utf16Length(utf8, span.offset + span.length) - from});
    }
    return ranges;
}

std::optional<MentionTyping> mentionTypedAt(const QString& before)
{
    const int sign = static_cast<int>(before.lastIndexOf(kMentionSign));
    if (sign < 0 || sign + 1 >= before.size()) {
        return std::nullopt;
    }
    const QString typed = before.sliced(sign + 1);
    if (std::any_of(typed.cbegin(), typed.cend(), [](const QChar c) { return c.isSpace(); })) {
        return std::nullopt;
    }
    // The opening rule is the reader's: a mention of exactly the typed run must start at this '@'.
    const std::string head = before.left(sign).toStdString();
    const std::vector<client::MentionSpan> spans
        = client::findMentions(before.toStdString(), {typed.toStdString()});
    const bool opens = std::any_of(spans.begin(), spans.end(),
        [&head](const client::MentionSpan& span) { return span.offset == head.size(); });
    if (!opens) {
        return std::nullopt;
    }
    return MentionTyping{sign, typed};
}

QStringList mentionChoices(const QStringList& members, const QString& own, const QString& typed)
{
    const client::NameFold fold = [](const std::string& name) {
        return QString::fromStdString(name).toCaseFolded().toStdString();
    };
    QStringList choices;
    for (const std::string& name : client::mentionCandidates(
             utf8Names(members), own.toStdString(), typed.toStdString(), fold)) {
        choices << QString::fromStdString(name);
    }
    return choices;
}

QVariantMap mentionOffer(const QString& before, const QStringList& members, const QString& own)
{
    QVariantMap offer;
    if (members.isEmpty()) {
        return offer;
    }
    const std::optional<MentionTyping> typing = mentionTypedAt(before);
    if (!typing) {
        return offer;
    }
    const QStringList choices = mentionChoices(members, own, typing->typed);
    if (choices.isEmpty()) {
        return offer;
    }
    offer[QStringLiteral("from")] = typing->from;
    offer[QStringLiteral("choices")] = choices;
    return offer;
}

QString mentionText(const QString& name)
{
    return kMentionSign + QString::fromStdString(client::mentionForm(name.toStdString()));
}

QStringList namesInOrder(QStringList names, const QString& language)
{
    QCollator collator{QLocale(language)};
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(names.begin(), names.end(),
        [&collator](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    return names;
}

QStringList namesMatching(const QStringList& names, const QString& query)
{
    const QString wanted = query.trimmed();
    if (wanted.isEmpty()) {
        return names;
    }
    QStringList found;
    for (const QString& name : names) {
        if (name.contains(wanted, Qt::CaseInsensitive)
            || mentionText(name).contains(wanted, Qt::CaseInsensitive)) {
            found << name;
        }
    }
    return found;
}

}  // namespace bazarish::app
