// Bazarish project (c) 2026
#pragma once

#include <QString>
#include <QStringList>

namespace bazarish::app {

bool isServiceMessage(const QString& type);

QString encodeSystemNote(const QString& sourceText, const QStringList& args = {});
// The body is kept for a version that can show the type.
QString encodeUnsupportedMessage(const QString& type, const QString& keptBody);
QString systemNoteText(const QString& stored);

QString chatTimerDurationNote(qint64 seconds);
QString chatTimerNote(qint64 seconds, bool ours, const QString& peerName);
QString chatClearedNote(bool ours, const QString& peerName);

}  // namespace bazarish::app
