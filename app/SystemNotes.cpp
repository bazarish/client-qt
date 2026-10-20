// Bazarish project (c) 2026
#include "SystemNotes.hpp"

#include "Session.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

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
    // An argument may be a note of its own, translated in turn.
    for (const QJsonValue& arg : args) {
        text = text.arg(systemNoteText(arg.toString()));
    }
    return text;
}

QString chatTimerDurationNote(const qint64 seconds)
{
    using namespace bazarish::client;
    switch (seconds) {
    case kChatTimerOff:
        return encodeSystemNote(QT_TR_NOOP("Off"));
    case kSecondsPerHour:
        return encodeSystemNote(QT_TR_NOOP("1 hour"));
    case kSecondsPerDay:
        return encodeSystemNote(QT_TR_NOOP("1 day"));
    case kDaysPerWeek * kSecondsPerDay:
        return encodeSystemNote(QT_TR_NOOP("7 days"));
    case kDaysPerMonth * kSecondsPerDay:
        return encodeSystemNote(QT_TR_NOOP("30 days"));
    }
    throw std::logic_error("not an auto-delete timer a chat can carry");
}

QString chatTimerNote(const qint64 seconds, const bool ours, const QString& peerName)
{
    if (seconds == bazarish::client::kChatTimerOff) {
        return ours ? encodeSystemNote(QT_TR_NOOP("You turned off the auto-delete timer"))
                    : encodeSystemNote(
                          QT_TR_NOOP("%1 turned off the auto-delete timer"), {peerName});
    }
    const QString duration = chatTimerDurationNote(seconds);
    return ours ? encodeSystemNote(QT_TR_NOOP("You set the auto-delete timer to %1"), {duration})
                : encodeSystemNote(
                      QT_TR_NOOP("%1 set the auto-delete timer to %2"), {peerName, duration});
}

}  // namespace bazarish::app
