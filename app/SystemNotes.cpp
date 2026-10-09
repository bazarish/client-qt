// Bazarish project (c) 2026
#include "SystemNotes.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace bazarish::app {

namespace {

const QString kNoteField = QStringLiteral("note");
const QString kArgsField = QStringLiteral("args");
const char* const kNoteContext = "SystemNote";

// A stored note keeps the wording it was written with.
QString currentWording(const QString& source)
{
    static const QHash<QString, QString> kReworded{
        {QStringLiteral("Request sent, awaiting delivery…"), QStringLiteral("Request sent.")},
    };
    return kReworded.value(source, source);
}

}  // namespace

bool isServiceMessage(const QString& type)
{
    return type == QStringLiteral("system") || type == QStringLiteral("contact.failed")
        || type == QStringLiteral("routing.failed");
}

QString encodeSystemNote(const QString& sourceText, const QStringList& args)
{
    QJsonObject note{{kNoteField, sourceText}};
    if (!args.isEmpty()) {
        note.insert(kArgsField, QJsonArray::fromStringList(args));
    }
    return QString::fromUtf8(QJsonDocument(note).toJson(QJsonDocument::Compact));
}

QString systemNoteText(const QString& stored)
{
    const QJsonObject note = QJsonDocument::fromJson(stored.toUtf8()).object();
    const QString source = note.value(kNoteField).toString();
    if (source.isEmpty()) {
        return stored;
    }
    QString text = QCoreApplication::translate(
        kNoteContext, currentWording(source).toUtf8().constData());
    const QJsonArray args = note.value(kArgsField).toArray();
    for (const QJsonValue& arg : args) {
        text = text.arg(arg.toString());
    }
    return text;
}

}  // namespace bazarish::app
