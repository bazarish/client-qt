// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>
#include <bazarish/Clock.hpp>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace bazarish::app {

QString humanBytes(qint64 bytes);

inline QByteArray toQByteArray(const ByteView bytes)
{
    return QByteArray(
        reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

QString waveformHex(const Bytes& opus);

QString newE2eId();

QString aliasChatPeer(const QString& alias);

bool hasAliasSigil(const QString& text);
QString reactionFlashKey(const QString& peer, const QString& target);
const QStringList& standardReactions();

inline const QString kAliasOperationId = QStringLiteral("alias");

}  // namespace bazarish::app
