// Bazarish project (c) 2026
#pragma once

#include <QtGlobal>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>

namespace bazarish::app {

inline constexpr std::size_t kSampleBytes = sizeof(std::int16_t);

// Qt hands audio over as bytes with no alignment promise, so samples cross by copy.
template <class Next>
qint64 fillSamples(char* const data, const qint64 maxLen, Next&& next)
{
    const std::size_t count = static_cast<std::size_t>(maxLen) / kSampleBytes;
    for (std::size_t i = 0; i < count; ++i) {
        const std::int16_t sample = next();
        std::memcpy(data + i * kSampleBytes, &sample, kSampleBytes);
    }
    return static_cast<qint64>(count * kSampleBytes);
}

inline void appendSamples(std::deque<std::int16_t>& ring, const char* const data, const qint64 len)
{
    const std::size_t count = static_cast<std::size_t>(len) / kSampleBytes;
    for (std::size_t i = 0; i < count; ++i) {
        std::int16_t sample = 0;
        std::memcpy(&sample, data + i * kSampleBytes, kSampleBytes);
        ring.push_back(sample);
    }
}

}  // namespace bazarish::app
