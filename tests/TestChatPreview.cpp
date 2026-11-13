// Bazarish project (c) 2026
#include "ChatPreview.hpp"
#include "TestUtil.hpp"

#include <cstdio>
#include <cstdlib>

using bazarish::app::TranscriptStore;
using bazarish::app::chatPreviewOf;

namespace {

QString previewOf(const char* const text, const char* const author = "")
{
    TranscriptStore::LastMessage last;
    last.text = QString::fromUtf8(text);
    last.type = QStringLiteral("text");
    last.author = QString::fromUtf8(author);
    return chatPreviewOf(last);
}

}  // namespace

int main()
{
    CHECK(previewOf("**loud** and *soft* and ~~gone~~")
        == QStringLiteral("loud and soft and gone"));
    CHECK(previewOf("press !!/help!! now") == QStringLiteral("press /help now"));
    CHECK(previewOf("ask !bob at https://example.i2p/a_b")
        == QStringLiteral("ask !bob at https://example.i2p/a_b"));
    CHECK(previewOf("run ```make **all**``` now") == QStringLiteral("run make **all** now"));
    CHECK(previewOf("2*3*4 and a ~~ lone pair")
        == QStringLiteral("2*3*4 and a ~~ lone pair"));
    CHECK(previewOf("**~~both~~**") == QStringLiteral("~~both~~"));
    CHECK(previewOf("**news**", "**Ann**") == QStringLiteral("**Ann**: news"));

    TranscriptStore::LastMessage file;
    file.type = QStringLiteral("file");
    file.attachment = QStringLiteral("**draft**.txt");
    CHECK(chatPreviewOf(file).endsWith(QStringLiteral(" **draft**.txt")));

    std::printf("TestChatPreview: all checks passed\n");
    return 0;
}
