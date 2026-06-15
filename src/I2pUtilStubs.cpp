// Bazarish project (c) 2026
//
// Minimal leaf-function definitions for the trimmed libi2pd "keys" build.
// The identity/crypto translation units reference a few i2p::util time and
// thread helpers that otherwise live in Timestamp.cpp — which transitively
// pulls in the whole router (RouterContext, transports, net stack). The
// client only manufactures key material, so we supply these leaves directly
// and keep the router out of the build entirely.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace i2p {
namespace util {

uint64_t GetSecondsSinceEpoch()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

namespace {

void dateString(uint64_t timestamp, char* date)
{
    const std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::snprintf(date, 9, "%04i%02i%02i", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

}  // namespace

void GetCurrentDate(char* date)
{
    dateString(GetSecondsSinceEpoch(), date);
}

void GetNextDayDate(char* date)
{
    dateString(GetSecondsSinceEpoch() + 24 * 60 * 60, date);
}

void SetThreadName(const char*)
{
    // The client's key-only use of libi2pd has no router threads to name.
}

}  // namespace util
}  // namespace i2p
