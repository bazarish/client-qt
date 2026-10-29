// Bazarish project (c) 2026
#include "ChatPreview.hpp"

#include "SystemNotes.hpp"

#include <QObject>

namespace bazarish::app {
namespace {

QString attachmentLabel(const QString& type)
{
    if (type == QStringLiteral("audio")) {
        return QObject::tr("[voice]");
    }
    if (type == QStringLiteral("image")) {
        return QObject::tr("[image]");
    }
    return QObject::tr("[file]");
}

}  // namespace

QString chatPreviewOf(const TranscriptStore::LastMessage& last)
{
    QString preview;
    if (!last.text.isEmpty()) {
        preview = isServiceMessage(last.type) ? systemNoteText(last.text) : last.text;
    } else if (last.type.isEmpty()) {
        return {};
    } else if (last.attachment.isEmpty()) {
        preview = attachmentLabel(last.type);
    } else {
        preview = attachmentLabel(last.type) + QLatin1Char(' ') + last.attachment;
    }
    return last.author.isEmpty() ? preview : QObject::tr("%1: %2").arg(last.author, preview);
}

}  // namespace bazarish::app
