// Bazarish project (c) 2026
#include "SessionShared.hpp"

#include "AudioCodec.hpp"
#include "Session.hpp"

#include <bazarish/Address.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QChar>
#include <QStringList>
#include <QDateTime>

#include <cstdint>
#include <vector>

namespace bazarish::app {

namespace {
constexpr qint64 kBytesPerUnit = 1024;
constexpr int kLargestUnit = 3;
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
    try {
        return QString::fromStdString(bazarish::voiceWaveformHex(opus));
    } catch (const std::exception& error) {
        bazarish::log::warn("voice waveform: {}", error.what());
        return {};
    }
}

QString newE2eId()
{
    return QString::fromStdString(bazarish::client::newMessageId());
}

QString aliasChatPeer(const QString& alias)
{
    return QChar::fromLatin1(bazarish::kAliasSigil)
        + QString::fromStdString(bazarish::aliasQueryText(
            bazarish::parseAliasQuery(alias.trimmed().toStdString())));
}

bool hasAliasSigil(const QString& text)
{
    return text.startsWith(QChar::fromLatin1(bazarish::kAliasSigil));
}

const QStringList& standardReactions()
{
    static const QStringList kStandard = {QStringLiteral("\U0001F44D"),
        QStringLiteral("\u2764\uFE0F"), QStringLiteral("\U0001F602"),
        QStringLiteral("\U0001F389"), QStringLiteral("\U0001F525"), QStringLiteral("\U0001F62E"),
        QStringLiteral("\U0001F622"), QStringLiteral("\U0001F64F"), QStringLiteral("\U0001F440"),
        QStringLiteral("\u2705"), QStringLiteral("\U0001F4AF"), QStringLiteral("\U0001F680"),
        QStringLiteral("\U0001F621"), QStringLiteral("\U0001F44F"), QStringLiteral("\U0001F914"),
        QStringLiteral("\U0001F44E"), QStringLiteral("\U0001F91D"), QStringLiteral("\U0001F529")};
    return kStandard;
}

QString reactionFlashKey(const QString& peer, const QString& target)
{
    return peer + QLatin1Char('\n') + target;
}

}  // namespace bazarish::app
