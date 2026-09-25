// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"





#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

// Qt makes `emit` a macro and the log header declares a function of that name,
// so the keyword is stood down for the length of this include.
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <chrono>
#include <cstring>
#include <QUrl>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace bazarish::app {

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

// Files in both directions: what a transfer is doing and where it lands.

void SessionController::saveAttachmentToFile(const QString& peer, const QString& e2eId,
    const QString& fileUrl, qint64 token)
{
    const QString dest = QUrl(fileUrl).toLocalFile();
    if (dest.isEmpty()) {
        conversation_.finishDownloadForId(
            token, false, QStringLiteral("Choose where to save the file."));
        return;
    }
    // Mark the message as downloading at once, so the bubble shows activity even
    // before the first byte-progress callback arrives.
    conversation_.setDownloadProgressForId(token, 0, 0);
    // Remember the destination so a successful download can record where it landed
    // (for the later "Open" action).
    pendingSavePath_.insert(token, dest);
    // The row carries the transfer's id, which is what stops it: a download that
    // has outlived its point must be endable from the activity panel, the same
    // way a send is.
    beginOperation(QStringLiteral("download:") + QString::number(token), QStringLiteral("file-down"),
        QFileInfo(dest).fileName(), QStringLiteral("Connecting…"), activePeer_, e2eId);
    emit requestSaveAttachment(peer, e2eId, dest, token);
}

QUrl SessionController::defaultSaveUrl(const QString& fileName) const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    const QString name = fileName.isEmpty() ? QStringLiteral("file") : fileName;
    return QUrl::fromLocalFile(QDir(dir).filePath(name));
}

void SessionController::onUploadProgress(qint64 localId, qint64 sent, qint64 total)
{
    const double fraction = total > 0 ? static_cast<double>(sent) / static_cast<double>(total) : 0.0;
    conversation_.setUploadProgressForId(localId, fraction);
    updateOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("Uploading…"),
        humanBytes(sent) + QStringLiteral(" / ") + humanBytes(total), fraction);
}

void SessionController::onDownloadProgress(qint64 token, qint64 received, qint64 total)
{
    conversation_.setDownloadProgressForId(token, received, total);
    const double fraction
        = total > 0 ? static_cast<double>(received) / static_cast<double>(total) : -1.0;
    updateOperation(QStringLiteral("download:") + QString::number(token),
        QStringLiteral("Downloading…"),
        total > 0 ? humanBytes(received) + QStringLiteral(" / ") + humanBytes(total) : QString(),
        fraction);
}

void SessionController::onTransferStage(
    const QString& peer, const QString& e2eId, const QString& stage)
{
    TransferProgress& progress = transfers_[e2eId];
    progress.peer = peer;
    progress.stage = stage;
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (m.id == 0) {
        return;
    }
    // A transfer runs long and can be stopped, so it gets a row of its own the
    // moment it starts - a send row from an hour ago is not that row.
    const QString opId = (m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
        + QString::number(m.id);
    if (operations_.indexOf(opId) < 0) {
        beginOperation(opId, m.outgoing ? QStringLiteral("file-up") : QStringLiteral("file-down"),
            m.attName.isEmpty() ? QStringLiteral("file") : m.attName, stage, peer, e2eId);
    } else {
        // The send's own row, opened when the file was announced: now there is a
        // transfer behind it, and it is stoppable.
        operations_.setCancelId(opId, e2eId);
    }
    // The row exists whether or not this conversation is on screen; the model
    // only has it while it is, and replayTransfersForActivePeer puts it back.
    if (peer == activePeer_) {
        conversation_.setTransferStageForId(m.id, stage);
    }
    updateOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
            + QString::number(m.id),
        stage);
}

void SessionController::onServedProgress(
    const QString& peer, const QString& e2eId, qint64 sent, qint64 total)
{
    TransferProgress& progress = transfers_[e2eId];
    progress.peer = peer;
    progress.sent = sent;
    progress.total = total;
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (m.id == 0) {
        return;
    }
    const double fraction
        = total > 0 ? static_cast<double>(sent) / static_cast<double>(total) : -1.0;
    if (peer == activePeer_) {
        if (m.outgoing) {
            conversation_.setUploadProgressForId(m.id, fraction);
        } else {
            conversation_.setDownloadProgressForId(m.id, sent, total);
        }
    }
    updateOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
            + QString::number(m.id),
        progress.stage,
        total > 0 ? humanBytes(sent) + QStringLiteral(" / ") + humanBytes(total) : QString(),
        fraction);
}

void SessionController::onServedFinished(
    const QString& peer, const QString& e2eId, const bool ok, const QString& error)
{
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (m.id == 0 || peer != activePeer_) {
        // Nobody is looking at this conversation: remember the outcome so opening
        // it shows what happened, instead of a bubble that quietly lost its bar.
        TransferProgress& kept = transfers_[e2eId];
        kept.peer = peer;
        kept.stage.clear();
        kept.finished = true;
        kept.ok = ok;
        kept.error = error;
        if (!ok) {
            emit actionFailed(error);  // and say it now, wherever the user is
        }
        if (m.id != 0) {
            finishOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
                    + QString::number(m.id),
                ok, ok ? QStringLiteral("Transferred") : error);
        }
        return;
    }
    transfers_.remove(e2eId);
    if (!m.outgoing) {
        // An incoming transfer ends where a download ends: the saved path, the
        // bubble's own state and its row, all keyed by the message id.
        conversation_.setTransferStageForId(m.id, {});
        onDownloadFinished(m.id, ok, error);
        return;
    }
    if (peer == activePeer_) {
        conversation_.setUploadProgressForId(m.id, -1.0);
        conversation_.setTransferStageForId(m.id, {});
        if (!ok) {
            conversation_.setErrorForId(m.id, error);
        }
    }
    finishOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
            + QString::number(m.id),
        ok, ok ? QStringLiteral("Transferred") : error);
}

void SessionController::cancelTransfer(const QString& e2eId)
{
    emit requestCancelTransfer(e2eId);
}

void SessionController::replayTransfersForActivePeer()
{
    QStringList settled;
    for (auto it = transfers_.constBegin(); it != transfers_.constEnd(); ++it) {
        if (it.value().peer != activePeer_) {
            continue;
        }
        const StoredMessage m = store_.messageByE2e(it.key(), activePeer_);
        if (m.id == 0) {
            continue;
        }
        if (it.value().finished) {
            // The outcome arrived while this conversation was closed.
            conversation_.setTransferStageForId(m.id, {});
            if (m.outgoing) {
                conversation_.setUploadProgressForId(m.id, -1.0);
                if (!it.value().ok) {
                    conversation_.setErrorForId(m.id, it.value().error);
                }
            } else {
                conversation_.finishDownloadForId(m.id, it.value().ok, it.value().error);
            }
            settled << it.key();
            continue;
        }
        conversation_.setTransferStageForId(m.id, it.value().stage);
        if (it.value().total > 0) {
            if (m.outgoing) {
                conversation_.setUploadProgressForId(m.id,
                    static_cast<double>(it.value().sent) / static_cast<double>(it.value().total));
            } else {
                conversation_.setDownloadProgressForId(m.id, it.value().sent, it.value().total);
            }
        }
    }
    for (const QString& id : settled) {
        transfers_.remove(id);
    }
}

void SessionController::onDownloadFinished(qint64 token, bool ok, const QString& error)
{
    const QString path = pendingSavePath_.take(token);
    const QString opId = QStringLiteral("download:") + QString::number(token);
    finishOperation(opId, ok, ok ? QStringLiteral("Saved") : (QStringLiteral("Failed: ") + error));
    conversation_.finishDownloadForId(token, ok, error);
    if (ok && !path.isEmpty()) {
        // Remember where it landed, in the store and the open view, so the bubble
        // can offer to open it (falling back to re-save when the file is gone).
        store_.setSavedPath(token, path);
        conversation_.setSavedPathForId(token, path);
    }
}

bool SessionController::fileExists(const QString& path) const
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

void SessionController::showInFolder(const QString& path) const
{
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
    // Ask the desktop's file manager to reveal the file with it selected, via the
    // freedesktop.org FileManager1 D-Bus interface (Nautilus, Dolphin, Nemo, ...).
    if (info.exists()) {
        QDBusInterface fm(QStringLiteral("org.freedesktop.FileManager1"),
            QStringLiteral("/org/freedesktop/FileManager1"),
            QStringLiteral("org.freedesktop.FileManager1"), QDBusConnection::sessionBus());
        if (fm.isValid()) {
            const QStringList uris{QUrl::fromLocalFile(info.absoluteFilePath()).toString()};
            const QDBusReply<void> reply = fm.call(QStringLiteral("ShowItems"), uris, QString());
            if (reply.isValid()) {
                return;
            }
        }
    }
#endif
    // Fallback (no D-Bus file manager, or the call failed): open the containing
    // directory without a selection.
    const QString dir = info.absolutePath();
    if (!dir.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
}

}  // namespace bazarish::app
