// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <QString>

namespace bazarish::app {

qint64 nowMillis();

QString humanBytes(qint64 bytes);

QString waveformHex(const Bytes& opus);

QString newE2eId();

inline const QString kAliasOperationId = QStringLiteral("alias");

}  // namespace bazarish::app
