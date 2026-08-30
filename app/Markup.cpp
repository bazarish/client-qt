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

// Longest first: tried in this order, so a bold pair is never read as an empty
// italic one followed by a stray asterisk.
constexpr std::array<StyleMarker, 3> kStyleMarkers{{
    {QLatin1String("**"), StyleKind::eBold},
    {QLatin1String("~~"), StyleKind::eStrike},
    {QLatin1String("*"), StyleKind::eItalic},
}};

// What wraps text the message offers to send back with one click.
constexpr QLatin1String kSendMarker("!!");
// What introduces a username.
constexpr QChar kAliasMarker(u'!');

constexpr std::array<QLatin1String, 2> kLinkSchemes{
    QLatin1String("https://"),
    QLatin1String("http://"),
};

// Punctuation that ends a sentence rather than the address inside it.
constexpr QStringView kTrailingPunctuation = u".,;:!?'\"\u00bb";
// Closing brackets are only dropped when the address does not open them itself.
struct Bracket {
    QChar open;
    QChar close;
};
constexpr std::array<Bracket, 3> kBrackets{{
    {QChar(u'('), QChar(u')')},
    {QChar(u'['), QChar(u']')},
    {QChar(u'{'), QChar(u'}')},
}};

// The schemes the interface answers a click on. Ours alone: they are built here
// and never come from a message, which is escaped before it reaches the document.
constexpr QLatin1String kSendScheme("bz-send:");
constexpr QLatin1String kAliasScheme("bz-alias:");

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

// The grammar the add-a-contact form actually resolves: letters and digits only,
// case folded when it is looked up. Deliberately not the wider address grammar -
// highlighting a name the form would then refuse is worse than not marking it.
bool isAliasChar(const QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9');
}

// What joins the parts of a name elsewhere in the project. A name that carries
// one is not a name this form can look up.
bool isNameSeparator(const QChar c)
{
    return c == u'_' || c == u'.' || c == u'-';
}

// A span never crosses a line: an unclosed marker then costs one line rather
// than swallowing the rest of the message.
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

    // A style span holds text and the clickable forms, but not another style: two
    // levels are what the client renders, and nesting emphasis inside emphasis
    // reads as a parser that guessed rather than as formatting.
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
            // Opens at a word boundary and against text, not against a space:
            // "2*3*4" is arithmetic and stays as typed.
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
            // Nothing to send is not an offer to send it.
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
        // A scheme with no host behind it is not an address.
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

    // Drops what closes the sentence rather than the address.
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
        // A name runs to a boundary: what follows may not be a name character in
        // any alphabet, or this is an ordinary exclamation inside a word.
        if (end < to && isWordChar(text_[end])) {
            return false;
        }
        // "!bob_smith" is one word to whoever wrote it. Marking "bob" out of it
        // would open the form on a name nobody typed, so it is left as text; a
        // stop that ends a sentence still ends the name.
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
            // A rich text document folds a run of spaces into one; a message that
            // lined something up with spaces keeps its shape.
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

QString href(const Run& run)
{
    switch (run.action) {
    case Action::eLink:
        return escaped(run.target);
    case Action::eSend:
        return kSendScheme + QString::fromLatin1(QUrl::toPercentEncoding(run.target));
    case Action::eAlias:
        return kAliasScheme + QString::fromLatin1(QUrl::toPercentEncoding(run.target));
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

QString toHtml(const QString& text, const QString& actionColor, const QString& chipColor)
{
    QString out;
    for (const Run& run : parse(text)) {
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
        // The colour is written into the run as well as onto the anchor: a
        // document draws an anchor in its own link colour unless the text under
        // it carries one, and this one has no palette to set.
        QString style = QLatin1String("color:") + actionColor + QLatin1String(";");
        style += run.action == Action::eLink ? QLatin1String("text-decoration:underline;")
                                             : QLatin1String("text-decoration:none;");
        // What acts inside the client stands on a ground of its own; what leaves
        // it is underlined. Left plain, a name in a sentence reads as a sentence.
        if (run.action != Action::eLink) {
            style += QLatin1String("background-color:") + chipColor + QLatin1String(";");
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
