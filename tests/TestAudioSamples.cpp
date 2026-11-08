// Bazarish project (c) 2026
#include "AudioSamples.hpp"
#include "TestUtil.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>

namespace {

constexpr std::size_t kSamples = 5;
constexpr std::int16_t kFirst = -3;

}  // namespace

int main()
{
    using bazarish::app::kSampleBytes;
    std::array<char, kSamples * kSampleBytes + 2> buffer{};
    char* const odd = buffer.data() + 1;

    std::int16_t next = kFirst;
    const qint64 written = bazarish::app::fillSamples(
        odd, static_cast<qint64>(kSamples * kSampleBytes + 1), [&next]() { return next++; });
    CHECK(written == static_cast<qint64>(kSamples * kSampleBytes));
    for (std::size_t i = 0; i < kSamples; ++i) {
        std::int16_t sample = 0;
        std::memcpy(&sample, odd + i * kSampleBytes, kSampleBytes);
        CHECK(sample == static_cast<std::int16_t>(kFirst + static_cast<int>(i)));
    }

    std::deque<std::int16_t> ring;
    bazarish::app::appendSamples(ring, odd, written + 1);
    CHECK(ring.size() == kSamples);
    CHECK(ring.front() == kFirst);
    CHECK(ring.back() == static_cast<std::int16_t>(kFirst + static_cast<int>(kSamples) - 1));
    return 0;
}
