// Bazarish project (c) 2026
#include "MailboxFill.hpp"

#include <cstdio>
#include <cstdlib>

using namespace bazarish::app;

namespace {

constexpr std::uint64_t kQuota = 1000;

}  // namespace

static_assert(!mailboxNearlyFull(0, 0));
static_assert(!mailboxNearlyFull(kQuota * kMailboxWarnPercent / kPercent - 1, kQuota));
static_assert(mailboxNearlyFull(kQuota * kMailboxWarnPercent / kPercent, kQuota));
static_assert(!mailboxFull(kQuota - 1, kQuota));
static_assert(mailboxNearlyFull(kQuota, kQuota) && mailboxFull(kQuota, kQuota));
static_assert(!mailboxFull(1, 0));

int main()
{
    std::printf("TestMailboxFill OK\n");
    return EXIT_SUCCESS;
}
