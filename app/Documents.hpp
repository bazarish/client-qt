// Bazarish project (c) 2026
#pragma once

#include "FileTransfer.hpp"

#include <QDir>
#include <QString>
#include <QUrl>

#include <memory>

namespace bazarish::app {

// A file picked on Android is a content:// document that only its provider can open.
inline bool isDocumentUri(const QString& target)
{
    return target.startsWith(QStringLiteral("content:"));
}

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

std::shared_ptr<const client::FileSource> deviceFiles();

Bytes readWhole(const QString& location);

std::unique_ptr<client::FileOutput> openOutput(const QString& dest);

void saveIntoFolder(const QDir& folder, const QString& fileName, const QByteArray& bytes);
void saveIntoDownloads(const QString& fileName, const QString& mime, const QByteArray& bytes);

// A document's provider offers no neighbour, so its ciphertext gathers in app storage.
client::FileTarget fileTargetFor(const QString& dest, const QString& partName);

}  // namespace bazarish::app
