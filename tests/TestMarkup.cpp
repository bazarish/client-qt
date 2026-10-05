// Bazarish project (c) 2026
#include "Markup.hpp"

#include <bazarish/Address.hpp>

#include <QString>

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

using namespace bazarish::app::markup;

namespace {

const QString kOverlongAlias = QString(bazarish::kAliasMaxLength + 1, QChar(u'a'));

const Colors kColors{QStringLiteral("#f2f4f2"), QStringLiteral("#232a31"),
    QStringLiteral("#11151a"), QStringLiteral("#d7dbd8")};

std::vector<Run> runsOf(const char* const text)
{
    return parse(QString::fromUtf8(text));
}

QString html(const char* const text)
{
    return toHtml(QString::fromUtf8(text), kColors);
}

QString plain(const char* const text)
{
    return toPlain(QString::fromUtf8(text));
}

void testStyles()
{
    const std::vector<Run> bold = runsOf("say **loud** now");
    CHECK(bold.size() == 3);
    CHECK(bold[1].text == QStringLiteral("loud"));
    CHECK(bold[1].bold && !bold[1].italic && !bold[1].strike);
    CHECK(bold[2].text == QStringLiteral(" now"));

    const std::vector<Run> italic = runsOf("*leaning*");
    CHECK(italic.size() == 1);
    CHECK(italic[0].italic && !italic[0].bold);

    const std::vector<Run> strike = runsOf("~~gone~~");
    CHECK(strike.size() == 1);
    CHECK(strike[0].strike);
    CHECK(strike[0].text == QStringLiteral("gone"));

    CHECK(plain("2*3*4") == QStringLiteral("2*3*4"));
    CHECK(runsOf("2*3*4").size() == 1);
    CHECK(!runsOf("2*3*4")[0].italic);

    CHECK(!runsOf("* not a list")[0].italic);
    CHECK(!runsOf("**hanging")[0].bold);
    CHECK(!runsOf("*over\nthe edge*")[0].italic);

    const std::vector<Run> nested = runsOf("**bold *and* more**");
    CHECK(nested.size() == 1);
    CHECK(nested[0].bold && !nested[0].italic);
    CHECK(nested[0].text == QStringLiteral("bold *and* more"));

    const std::vector<Run> cyrillic = runsOf("вот **жирный** текст");
    CHECK(cyrillic.size() == 3);
    CHECK(cyrillic[1].bold);
    CHECK(cyrillic[1].text == QString::fromUtf8("жирный"));
}

void testSendable()
{
    const std::vector<Run> offered = runsOf("press !!/help!! for help");
    CHECK(offered.size() == 3);
    CHECK(offered[1].action == Action::eSend);
    CHECK(offered[1].text == QStringLiteral("/help"));
    CHECK(offered[1].target == QStringLiteral("/help"));

    CHECK(runsOf("!!   !!")[0].action == Action::eNone);
    CHECK(runsOf("!!!!")[0].action == Action::eNone);

    const std::vector<Run> literal = runsOf("!!**not bold**!!");
    CHECK(literal.size() == 1);
    CHECK(literal[0].action == Action::eSend);
    CHECK(literal[0].text == QStringLiteral("**not bold**"));

    const std::vector<Run> inside = runsOf("**press !!/help!!**");
    CHECK(inside.size() == 2);
    CHECK(inside[0].bold && inside[0].text == QStringLiteral("press "));
    CHECK(inside[1].bold && inside[1].action == Action::eSend);
    CHECK(inside[1].target == QStringLiteral("/help"));

    CHECK(html("!!/help me!!").contains(QStringLiteral("bz-send:%2Fhelp%20me")));
}

void testLinks()
{
    const std::vector<Run> plainLink = runsOf("see https://example.i2p/page now");
    CHECK(plainLink.size() == 3);
    CHECK(plainLink[1].action == Action::eLink);
    CHECK(plainLink[1].target == QStringLiteral("https://example.i2p/page"));

    CHECK(runsOf("go to http://example.i2p.")[1].target == QStringLiteral("http://example.i2p"));
    CHECK(runsOf("(https://example.i2p)")[1].target == QStringLiteral("https://example.i2p"));
    CHECK(runsOf("https://example.i2p/a_(b)")[0].target
        == QStringLiteral("https://example.i2p/a_(b)"));

    CHECK(runsOf("HTTPS://example.i2p")[0].action == Action::eLink);
    CHECK(runsOf("nothttps://example.i2p")[0].action == Action::eNone);
    CHECK(runsOf("https://")[0].action == Action::eNone);
    CHECK(runsOf("file:///etc/passwd")[0].action == Action::eNone);
    CHECK(runsOf("bazarish://invite")[0].action == Action::eNone);
}

void testAliases()
{
    const std::vector<Run> named = runsOf("ask !bob about it");
    CHECK(named.size() == 3);
    CHECK(named[1].action == Action::eAlias);
    CHECK(named[1].text == QStringLiteral("!bob"));
    CHECK(named[1].target == QStringLiteral("bob"));

    CHECK(runsOf("!Bob")[0].target == QStringLiteral("Bob"));
    CHECK(runsOf("Wow!Great")[0].action == Action::eNone);
    const std::vector<Run> stopped = runsOf("ask !bob.");
    CHECK(stopped[1].target == QStringLiteral("bob"));
    CHECK(stopped[2].text == QStringLiteral("."));
    CHECK(runsOf("!bob_smith").size() == 1);
    CHECK(runsOf("!bob_smith")[0].action == Action::eNone);
    CHECK(runsOf("!bob.smith")[0].action == Action::eNone);
    CHECK(parse(QChar(u'!') + kOverlongAlias)[0].action == Action::eNone);
    CHECK(runsOf("!")[0].action == Action::eNone);
    CHECK(runsOf("!!bob!!")[0].action == Action::eSend);
    CHECK(runsOf("!!bob")[0].action == Action::eNone);
}

void testHtmlIsOurs()
{
    const QString injected = html("<img src=\"http://tracker.example/x.png\">");
    CHECK(!injected.contains(QStringLiteral("<img")));
    CHECK(injected.contains(QStringLiteral("&lt;img")));
    CHECK(injected.contains(QStringLiteral("&quot;")));
    CHECK(html("a & b").contains(QStringLiteral("a &amp; b")));
    CHECK(html("one\ntwo").contains(QStringLiteral("<br>")));
    CHECK(html("a   b").contains(QStringLiteral("&nbsp;")));

    CHECK(html("**loud**").contains(QStringLiteral("<b>loud</b>")));
    CHECK(html("*leaning*").contains(QStringLiteral("<i>leaning</i>")));
    CHECK(html("~~gone~~").contains(QStringLiteral("<s>gone</s>")));

    CHECK(html("!!/help!!").contains(QStringLiteral("background-color:#232a31")));
    CHECK(html("!bob").contains(QStringLiteral("background-color:#232a31")));
    CHECK(!html("https://example.i2p").contains(QStringLiteral("background-color")));
    CHECK(html("https://example.i2p").contains(QStringLiteral("text-decoration:underline")));

    CHECK(html("https://example.i2p/a?b=1&c=2")
              .contains(QStringLiteral("href=\"https://example.i2p/a?b=1&amp;c=2\"")));
}

void testCodeBlocks()
{
    const std::vector<Run> block = runsOf("run ```make -j4``` now");
    CHECK(block.size() == 3);
    CHECK(block[0].text == QStringLiteral("run "));
    CHECK(block[1].action == Action::eCopy);
    CHECK(block[1].text == QStringLiteral("make -j4"));
    CHECK(block[1].target == block[1].text);
    CHECK(block[2].text == QStringLiteral(" now"));

    CHECK(runsOf("```**x**```")[0].text == QStringLiteral("**x**"));
    CHECK(runsOf("```**x**```")[0].action == Action::eCopy);

    CHECK(runsOf("```   ```").size() == 1);
    CHECK(runsOf("```   ```")[0].action == Action::eNone);
    CHECK(plain("```   ```") == QStringLiteral("```   ```"));

    const QString drawn = html("```make -j4```");
    CHECK(drawn.contains(QStringLiteral("bz-copy:make%20-j4")));
    CHECK(drawn.contains(QStringLiteral("font-family:monospace")));
    CHECK(drawn.contains(QStringLiteral("background-color:#11151a")));
    CHECK(drawn.contains(QStringLiteral("color:#d7dbd8")));
    CHECK(!drawn.contains(QStringLiteral("background-color:#232a31")));

    const QString shaped = html("```a  b\nc```");
    CHECK(shaped.contains(QStringLiteral("a&nbsp;&nbsp;b")));
    CHECK(shaped.contains(QStringLiteral("<br>")));

    const QString tagged = html("```<b>x</b>```");
    CHECK(!tagged.contains(QStringLiteral("<b>x</b>")));
    CHECK(tagged.contains(QStringLiteral("&lt;b&gt;x&lt;/b&gt;")));
}

void testPlainProjection()
{
    CHECK(plain("**loud** and *soft* and ~~gone~~") == QStringLiteral("loud and soft and gone"));
    CHECK(plain("press !!/help!!") == QStringLiteral("press /help"));
    CHECK(plain("ask !bob at https://example.i2p")
        == QStringLiteral("ask !bob at https://example.i2p"));
    CHECK(plain("run ```make``` now") == QStringLiteral("run make now"));
    CHECK(plain("") == QString());
}

}  // namespace

int main()
{
    testStyles();
    testSendable();
    testLinks();
    testAliases();
    testHtmlIsOurs();
    testCodeBlocks();
    testPlainProjection();
    std::fprintf(stderr, "TestMarkup passed\n");
    return 0;
}
