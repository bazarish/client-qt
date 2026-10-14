// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <QString>

namespace bazarish::app {

qint64 nowMillis();

QString humanBytes(qint64 bytes);

QString waveformHex(const Bytes& opus);

QString newE2eId();

// An alias add is shown in a chat of its own until the alias names a fingerprint.
QString aliasChatPeer(const QString& alias);

bool hasAliasSigil(const QString& text);

inline const QString kAliasOperationId = QStringLiteral("alias");

}  // namespace bazarish::app
