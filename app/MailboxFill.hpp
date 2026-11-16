// Bazarish project (c) 2026
#pragma once

#include <cstdint>

namespace bazarish::app {

inline constexpr std::uint64_t kPercent = 100;
inline constexpr std::uint64_t kMailboxWarnPercent = 85;

constexpr bool mailboxNearlyFull(const std::uint64_t usedBytes, const std::uint64_t quotaBytes)
{
    return quotaBytes > 0 && usedBytes * kPercent >= quotaBytes * kMailboxWarnPercent;
}

constexpr bool mailboxFull(const std::uint64_t usedBytes, const std::uint64_t quotaBytes)
{
    return quotaBytes > 0 && usedBytes >= quotaBytes;
}

}  // namespace bazarish::app
