// Bazarish project (c) 2026
#pragma once

#include <QString>
#include <QStringList>

namespace bazarish::app {

bool isServiceMessage(const QString& type);

QString encodeSystemNote(const QString& sourceText, const QStringList& args = {});
// The placeholder for a type this version cannot show; the body is kept for one that can.
QString encodeUnsupportedMessage(const QString& type, const QString& keptBody);
QString systemNoteText(const QString& stored);

// An auto-delete timer as a note: its duration, or that there is none.
QString chatTimerDurationNote(qint64 seconds);
// The plate that says who set a chat's auto-delete timer, and to what.
QString chatTimerNote(qint64 seconds, bool ours, const QString& peerName);

}  // namespace bazarish::app
