// Bazarish project (c) 2026
#include "SessionShared.hpp"

#include "AudioCodec.hpp"

// Qt makes `emit` a macro and the log header declares a function of that name,
// so the keyword is stood down for the length of this include.
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QChar>
#include <QDateTime>
#include <QRandomGenerator>

#include <cstdint>
#include <vector>

namespace bazarish::app {

namespace {
// Bars in a voice message's drawn shape.
constexpr int kVoiceWaveBars = 40;
constexpr qint64 kBytesPerUnit = 1024;
// KB, MB, GB, TB: the last index the loop may climb to.
constexpr int kLargestUnit = 3;
// Below this a size is written with a decimal; above it, whole units read better.
constexpr double kDecimalBelow = 10.0;
}  // namespace

qint64 nowMillis()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString humanBytes(const qint64 bytes)
{
    if (bytes < kBytesPerUnit) {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / kBytesPerUnit;
    int unit = 0;
    while (value >= kBytesPerUnit && unit < kLargestUnit) {
        value /= kBytesPerUnit;
        ++unit;
    }
    return QString::number(value, 'f', value < kDecimalBelow ? 1 : 0) + QChar(' ')
        + QString::fromLatin1(kUnits[unit]);
}

QString waveformHex(const Bytes& opus)
{
    std::vector<std::uint8_t> bars;
    try {
        bars = bazarish::voiceWaveform(opus, kVoiceWaveBars);
    } catch (const std::exception& error) {
        bazarish::log::warn("voice waveform: {}", error.what());
        return {};
    }
    QString hex;
    hex.reserve(static_cast<qsizetype>(bars.size()));
    for (const std::uint8_t bar : bars) {
        hex.append(QChar::fromLatin1("0123456789abcdef"[bar]));
    }
    return hex;
}

QString newE2eId()
{
    return QString::number(QRandomGenerator::global()->generate64(), 16);
}

}  // namespace bazarish::app
