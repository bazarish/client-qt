// Bazarish project (c) 2026
#pragma once

#include "FileTransfer.hpp"

#include <QString>
#include <QUrl>

#include <memory>

namespace bazarish::app {

// A file picked on Android is a content:// document that only its provider can open.
inline bool isDocumentUri(const QString& target)
{
    return target.startsWith(QStringLiteral("content:"));
}

// Where a file dialog's answer points: a local path, an Android document, or nothing.
inline QString dialogTarget(const QString& fileUrl)
{
    const QUrl url(fileUrl);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return isDocumentUri(fileUrl) ? fileUrl : QString();
}

struct FileFacts {
    QString name;
    qint64 size = -1;
};
FileFacts factsOf(const QString& location);

// The files this device reads: local paths, and on Android the documents the user picked.
std::shared_ptr<const client::FileSource> deviceFiles();

// A small file the user picked, read whole: a key or a backup.
Bytes readWhole(const QString& location);

// What is written whole goes here: a local file, or an Android document through its provider.
std::unique_ptr<client::FileOutput> openOutput(const QString& dest);

// A received file's ciphertext gathers beside a local destination; for a document,
// whose provider offers no neighbour, it gathers in the application's own storage.
client::FileTarget fileTargetFor(const QString& dest, const QString& partName);

}  // namespace bazarish::app
