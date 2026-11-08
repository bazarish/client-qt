// Bazarish project (c) 2026
#include "SessionShared.hpp"
#include "TestUtil.hpp"

namespace {

constexpr qint64 kKibibyte = 1024;
constexpr qint64 kTenKibibytes = 10 * kKibibyte;

}  // namespace

int main()
{
    using bazarish::app::humanBytes;
    CHECK(humanBytes(0) == QStringLiteral("0 B"));
    CHECK(humanBytes(kKibibyte - 1) == QStringLiteral("1023 B"));
    CHECK(humanBytes(kKibibyte) == QStringLiteral("1.0 KB"));
    CHECK(humanBytes(kTenKibibytes - 1) == QStringLiteral("10.0 KB"));
    CHECK(humanBytes(kTenKibibytes) == QStringLiteral("10 KB"));
    CHECK(humanBytes(kKibibyte * kKibibyte) == QStringLiteral("1.0 MB"));
    return 0;
}
