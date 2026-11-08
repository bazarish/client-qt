// Bazarish project (c) 2026
#include "TestUtil.hpp"
#include "Utf8Limit.hpp"

#include <bazarish/Limits.hpp>

#include <QString>

namespace {

constexpr int kCap = static_cast<int>(bazarish::kMaxAccountNameBytes);
constexpr int kCyrillicLetterBytes = 2;

QValidator::State verdict(const bazarish::app::Utf8Limit& limit, QString text)
{
    int position = static_cast<int>(text.size());
    return limit.validate(text, position);
}

}  // namespace

int main()
{
    bazarish::app::Utf8Limit limit;
    limit.setMaxBytes(kCap);

    CHECK(verdict(limit, QString(kCap, QChar('a'))) == QValidator::Acceptable);
    CHECK(verdict(limit, QString(kCap + 1, QChar('a'))) == QValidator::Invalid);
    CHECK(verdict(limit, QString(kCap / kCyrillicLetterBytes, QChar(0x044F)))
        == QValidator::Acceptable);
    CHECK(verdict(limit, QString(kCap / kCyrillicLetterBytes + 1, QChar(0x044F)))
        == QValidator::Invalid);
    CHECK(verdict(limit, QString()) == QValidator::Acceptable);
    return 0;
}
