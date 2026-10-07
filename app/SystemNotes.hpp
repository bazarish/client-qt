// Bazarish project (c) 2026
#pragma once

#include <QString>
#include <QStringList>

namespace bazarish::app {

bool isServiceMessage(const QString& type);

QString encodeSystemNote(const QString& sourceText, const QStringList& args = {});
QString systemNoteText(const QString& stored);

}  // namespace bazarish::app
