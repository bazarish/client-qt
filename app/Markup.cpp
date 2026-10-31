// Bazarish project (c) 2026
#include "Markup.hpp"

#include <bazarish/Address.hpp>

#include <QLatin1String>
#include <QStringView>
#include <QUrl>

#include <array>

namespace bazarish::app::markup {

namespace {

enum class StyleKind {
    eBold,
    eItalic,
    eStrike,
};

struct StyleMarker {
    QLatin1String text;
    StyleKind kind;
};

constexpr std::array<StyleMarker, 3> kStyleMarkers{{
    {QLatin1String("**"), StyleKind::eBold},
    {QLatin1String("~~"), StyleKind::eStrike},
    {QLatin1String("*"), StyleKind::eItalic},
}};

constexpr QLatin1String kSendMarker("!!");
constexpr QLatin1String kCodeMarker("```");
constexpr int kTabWidth = 4;
constexpr QChar kAliasMarker(u'!');

constexpr std::array<QLatin1String, 2> kLinkSchemes{
    QLatin1String("https://"),
    QLatin1String("http://"),
};

constexpr QStringView kTrailingPunctuation = u".,;:!?'\"\u00bb";
struct Bracket {
    QChar open;
    QChar close;
};
constexpr std::array<Bracket, 3> kBrackets{{
    {QChar(u'('), QChar(u')')},
    {QChar(u'['), QChar(u']')},
    {QChar(u'{'), QChar(u'}')},
}};

constexpr QLatin1String kSendScheme("bz-send:");
constexpr QLatin1String kAliasScheme("bz-alias:");
constexpr QLatin1String kCopyScheme("bz-copy:");

struct Style {
    bool bold = false;
    bool italic = false;
    bool strike = false;
};

Style with(Style style, const StyleKind kind)
{
    switch (kind) {
    case StyleKind::eBold:
        style.bold = true;
        break;
    case StyleKind::eItalic:
        style.italic = true;
        break;
    case StyleKind::eStrike:
        style.strike = true;
        break;
    }
    return style;
}

bool isWordChar(const QChar c)
{
    return c.isLetterOrNumber();
}

bool isAliasChar(const QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9');
}

bool isNameSeparator(const QChar c)
{
    return c == u'_' || c == u'.' || c == u'-';
}

int lineEnd(const QString& text, const int from, const int to)
{
    const int found = text.indexOf(QChar(u'\n'), from);
    return (found < 0 || found > to) ? to : found;
}

bool matchesAt(const QString& text, const int at, const int to, const QLatin1String what)
{
    return at + what.size() <= to && QStringView(text).sliced(at, what.size()) == what;
}

class Parser {
public:
    explicit Parser(const QString& text)
        : text_(text)
    {
    }

    std::vector<Run> parse()
    {
        scan(0, static_cast<int>(text_.size()), Style{}, true);
        return std::move(runs_);
    }

private:
    void flush(QString& buffer, const Style style)
    {
        if (buffer.isEmpty()) {
            return;
        }
        Run run;
        run.text = buffer;
        run.bold = style.bold;
        run.italic = style.italic;
        run.strike = style.strike;
        runs_.push_back(std::move(run));
        buffer.clear();
    }

    void emitRun(QString text, QString target, const Action action, const Style style)
    {
        Run run;
        run.text = std::move(text);
        run.target = std::move(target);
        run.action = action;
        run.bold = style.bold;
        run.italic = style.italic;
        run.strike = style.strike;
        runs_.push_back(std::move(run));
    }

    void scan(const int from, const int to, const Style style, const bool allowStyles)
    {
        QString buffer;
        int i = from;
        while (i < to) {
            int contentFrom = 0;
            int contentTo = 0;
            int after = 0;
            StyleKind kind = StyleKind::eBold;
            if (allowStyles && matchStyle(i, to, kind, contentFrom, contentTo, after)) {
                flush(buffer, style);
                scan(contentFrom, contentTo, with(style, kind), false);
                i = after;
                continue;
            }
            if (matchCode(i, to, contentFrom, contentTo, after)) {
                flush(buffer, style);
                const QString inner = text_.sliced(contentFrom, contentTo - contentFrom);
                emitRun(inner, inner, Action::eCopy, style);
                i = after;
                continue;
            }
            if (matchSend(i, to, contentFrom, contentTo, after)) {
                flush(buffer, style);
                const QString inner = text_.sliced(contentFrom, contentTo - contentFrom);
                emitRun(inner, inner, Action::eSend, style);
                i = after;
                continue;
            }
            if (matchLink(i, to, after)) {
                flush(buffer, style);
                const QString address = text_.sliced(i, after - i);
                emitRun(address, address, Action::eLink, style);
                i = after;
                continue;
            }
            if (matchAlias(i, to, after)) {
                flush(buffer, style);
                emitRun(text_.sliced(i, after - i), text_.sliced(i + 1, after - i - 1),
                    Action::eAlias, style);
                i = after;
                continue;
            }
            buffer += text_[i];
            ++i;
        }
        flush(buffer, style);
    }

    bool matchStyle(const int at, const int to, StyleKind& kind, int& contentFrom,
        int& contentTo, int& after) const
    {
        const int limit = lineEnd(text_, at, to);
        for (const StyleMarker& marker : kStyleMarkers) {
            const int length = marker.text.size();
            if (!matchesAt(text_, at, limit, marker.text)) {
                continue;
            }
            if (at > 0 && isWordChar(text_[at - 1])) {
                continue;
            }
            if (at + length >= limit || text_[at + length].isSpace()) {
                continue;
            }
            for (int j = at + length + 1; j + length <= limit; ++j) {
                if (!matchesAt(text_, j, limit, marker.text) || text_[j - 1].isSpace()) {
                    continue;
                }
                if (j + length < to && isWordChar(text_[j + length])) {
                    continue;
                }
                kind = marker.kind;
                contentFrom = at + length;
                contentTo = j;
                after = j + length;
                return true;
            }
        }
        return false;
    }

    bool matchCode(const int at, const int to, int& contentFrom, int& contentTo, int& after) const
    {
        if (!matchesAt(text_, at, to, kCodeMarker)) {
            return false;
        }
        const int length = kCodeMarker.size();
        for (int j = at + length; j + length <= to; ++j) {
            if (!matchesAt(text_, j, to, kCodeMarker)) {
                continue;
            }
            if (text_.sliced(at + length, j - at - length).trimmed().isEmpty()) {
                return false;
            }
            contentFrom = at + length;
            contentTo = j;
            after = j + length;
            return true;
        }
        return false;
    }

    bool matchSend(const int at, const int to, int& contentFrom, int& contentTo, int& after) const
    {
        const int limit = lineEnd(text_, at, to);
        if (!matchesAt(text_, at, limit, kSendMarker)) {
            return false;
        }
        const int length = kSendMarker.size();
        for (int j = at + length; j + length <= limit; ++j) {
            if (!matchesAt(text_, j, limit, kSendMarker)) {
                continue;
            }
            if (text_.sliced(at + length, j - at - length).trimmed().isEmpty()) {
                return false;
            }
            contentFrom = at + length;
            contentTo = j;
            after = j + length;
            return true;
        }
        return false;
    }

    bool matchLink(const int at, const int to, int& after) const
    {
        if (at > 0 && isWordChar(text_[at - 1])) {
            return false;
        }
        const int limit = lineEnd(text_, at, to);
        int start = -1;
        for (const QLatin1String scheme : kLinkSchemes) {
            if (at + scheme.size() <= limit
                && QStringView(text_).sliced(at, scheme.size()).compare(
                       scheme, Qt::CaseInsensitive)
                    == 0) {
                start = at + scheme.size();
                break;
            }
        }
        if (start < 0 || start >= limit || !isWordChar(text_[start])) {
            return false;
        }
        int end = start;
        while (end < limit && !text_[end].isSpace() && text_[end] != QChar(u'<')
            && text_[end] != QChar(u'>') && text_[end] != QChar(u'"')) {
            ++end;
        }
        end = trimTail(at, end);
        after = end;
        return end > start;
    }

    int trimTail(const int from, int end) const
    {
        bool trimmed = true;
        while (trimmed && end > from) {
            trimmed = false;
            const QChar last = text_[end - 1];
            if (kTrailingPunctuation.contains(last)) {
                --end;
                trimmed = true;
                continue;
            }
            for (const Bracket& bracket : kBrackets) {
                if (last != bracket.close) {
                    continue;
                }
                const QString address = text_.sliced(from, end - from);
                if (address.count(bracket.close) > address.count(bracket.open)) {
                    --end;
                    trimmed = true;
                }
                break;
            }
        }
        return end;
    }

    bool matchAlias(const int at, const int to, int& after) const
    {
        if (text_[at] != kAliasMarker) {
            return false;
        }
        // Not inside a word, and not the tail of an unclosed send marker.
        if (at > 0 && (isWordChar(text_[at - 1]) || text_[at - 1] == kAliasMarker)) {
            return false;
        }
        int end = at + 1;
        while (end < to && isAliasChar(text_[end])) {
            ++end;
        }
        const int length = end - at - 1;
        if (length < static_cast<int>(kAliasMinLength)
            || length > static_cast<int>(kAliasMaxLength)) {
            return false;
        }
        if (end + 1 < to && text_[end] == QChar::fromLatin1(kAliasWordSeparator)
            && isAliasChar(text_[end + 1])) {
            int wordEnd = end + 1;
            while (wordEnd < to && isAliasChar(text_[wordEnd])) {
                ++wordEnd;
            }
            if (wordEnd - end - 1 > static_cast<int>(kAliasWordMaxLength)) {
                return false;
            }
            end = wordEnd;
        }
        if (end < to && isWordChar(text_[end])) {
            return false;
        }
        // "!bob_smith" is one word: no alias is marked out of it.
        if (end + 1 < to && isNameSeparator(text_[end]) && isAliasChar(text_[end + 1])) {
            return false;
        }
        after = end;
        return true;
    }

    const QString& text_;
    std::vector<Run> runs_;
};

QString escaped(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        switch (c.unicode()) {
        case u'&':
            out += QLatin1String("&amp;");
            break;
        case u'<':
            out += QLatin1String("&lt;");
            break;
        case u'>':
            out += QLatin1String("&gt;");
            break;
        case u'"':
            out += QLatin1String("&quot;");
            break;
        case u'\n':
            out += QLatin1String("<br>");
            break;
        case u' ':
            // Qt rich text folds a run of spaces into one.
            out += (i > 0 && text[i - 1] == QChar(u' ')) ? QLatin1String("&nbsp;")
                                                         : QLatin1String(" ");
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

QString escapedVerbatim(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&':
            out += QLatin1String("&amp;");
            break;
        case u'<':
            out += QLatin1String("&lt;");
            break;
        case u'>':
            out += QLatin1String("&gt;");
            break;
        case u'"':
            out += QLatin1String("&quot;");
            break;
        case u'\n':
            out += QLatin1String("<br>");
            break;
        case u' ':
            out += QLatin1String("&nbsp;");
            break;
        case u'\t':
            for (int i = 0; i < kTabWidth; ++i) {
                out += QLatin1String("&nbsp;");
            }
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

QString href(const Run& run)
{
    switch (run.action) {
    case Action::eLink:
        return escaped(run.target);
    case Action::eSend:
        return kSendScheme + QString::fromLatin1(QUrl::toPercentEncoding(run.target));
    case Action::eAlias:
        return kAliasScheme + QString::fromLatin1(QUrl::toPercentEncoding(run.target));
    case Action::eCopy:
        return kCopyScheme + QString::fromLatin1(QUrl::toPercentEncoding(run.target));
    case Action::eNone:
        break;
    }
    return QString();
}

}  // namespace

std::vector<Run> parse(const QString& text)
{
    return Parser(text).parse();
}

QString toHtml(const QString& text, const Colors& colors)
{
    QString out;
    for (const Run& run : parse(text)) {
        if (run.action == Action::eCopy) {
            const QString style = QLatin1String("font-family:monospace;background-color:")
                + colors.code + QLatin1String(";color:") + colors.codeText
                + QLatin1String(";text-decoration:none;");
            out += QLatin1String("<a href=\"") + href(run) + QLatin1String("\" style=\"") + style
                + QLatin1String("\"><span style=\"") + style + QLatin1String("\">")
                + escapedVerbatim(run.text) + QLatin1String("</span></a>");
            continue;
        }
        QString body = escaped(run.text);
        if (run.bold) {
            body = QLatin1String("<b>") + body + QLatin1String("</b>");
        }
        if (run.italic) {
            body = QLatin1String("<i>") + body + QLatin1String("</i>");
        }
        if (run.strike) {
            body = QLatin1String("<s>") + body + QLatin1String("</s>");
        }
        if (run.action == Action::eNone) {
            out += body;
            continue;
        }
        QString style = QLatin1String("color:") + colors.action + QLatin1String(";");
        style += run.action == Action::eLink ? QLatin1String("text-decoration:underline;")
                                             : QLatin1String("text-decoration:none;");
        if (run.action != Action::eLink) {
            style += QLatin1String("background-color:") + colors.chip + QLatin1String(";");
        }
        out += QLatin1String("<a href=\"") + href(run) + QLatin1String("\" style=\"") + style
            + QLatin1String("\"><span style=\"") + style + QLatin1String("\">") + body
            + QLatin1String("</span></a>");
    }
    return out;
}

QString toPlain(const QString& text)
{
    QString out;
    for (const Run& run : parse(text)) {
        out += run.text;
    }
    return out;
}

}  // namespace bazarish::app::markup
