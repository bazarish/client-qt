// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "SessionShared.hpp"

#include <QFileInfo>

#include <algorithm>
#include <exception>

namespace bazarish::app {

namespace {

const QString kConnectOperationId = QStringLiteral("connect");
constexpr double kPercentFull = 100.0;

QString transferOpId(const StoredMessage& m)
{
    return (m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
        + QString::number(m.id);
}

QString downloadOpId(const qint64 token)
{
    return QStringLiteral("download:") + QString::number(token);
}

double fractionOf(const qint64 done, const qint64 total, const double unknown)
{
    return total > 0 ? static_cast<double>(done) / static_cast<double>(total) : unknown;
}

}  // namespace

void CoreAccount::onConnectRequested()
{
    connecting_ = true;
    connectPercent_ = 0;
    log_.begin(kConnectOperationId, QStringLiteral("connect"), tr("Connecting this account"),
        tr("Starting…"));
}

void CoreAccount::onConnectProgress(const int percent, const QString& phase)
{
    if (percent < connectPercent_) {
        return;
    }
    connectPercent_ = percent;
    log_.update(kConnectOperationId, phase, {}, percent / kPercentFull);
}

void CoreAccount::settleConnect(const bool connected, const QString& note)
{
    if (!connecting_) {
        return;
    }
    connecting_ = false;
    log_.finish(kConnectOperationId, connected, connected ? tr("Connected") : note);
}

void CoreAccount::sendCallback(
    const QString& peer, const QString& data, const QString& ref, const QString& label)
{
    const QString opId = QStringLiteral("bot:") + ref + QStringLiteral(":") + data;
    log_.begin(opId, QStringLiteral("bot"),
        (label.isEmpty() ? data : label) + QStringLiteral(" → ") + contacts_.peerName(peer),
        tr("Sending…"), peer);
    emit commands_.requestSendCallback(opId, peer, data, ref);
}

void CoreAccount::sendCommand(
    const QString& peer, const QString& command, const QString& args, const QString& label)
{
    const QString opId = QStringLiteral("bot-command:") + command;
    log_.begin(opId, QStringLiteral("bot"),
        (label.isEmpty() ? QStringLiteral("/") + command : label) + QStringLiteral(" → ")
            + contacts_.peerName(peer),
        tr("Sending…"), peer);
    emit commands_.requestSendCommand(opId, peer, command, args);
}

void CoreAccount::saveAttachment(
    const QString& peer, const QString& e2eId, const QString& dest, const qint64 token)
{
    pendingSavePath_.insert(token, dest);
    log_.begin(downloadOpId(token), QStringLiteral("file-down"), QFileInfo(dest).fileName(),
        tr("Connecting…"), peer, e2eId);
    emit commands_.requestSaveAttachment(peer, e2eId, dest, token);
}

void CoreAccount::onUploadProgress(const qint64 localId, const qint64 sent, const qint64 total)
{
    log_.update(QStringLiteral("send:") + QString::number(localId), tr("Uploading…"),
        humanBytes(sent) + QStringLiteral(" / ") + humanBytes(total),
        fractionOf(sent, total, 0.0));
}

void CoreAccount::onDownloadProgress(const qint64 token, const qint64 received, const qint64 total)
{
    log_.update(downloadOpId(token), tr("Downloading…"),
        total > 0 ? humanBytes(received) + QStringLiteral(" / ") + humanBytes(total) : QString(),
        fractionOf(received, total, kProgressUnknown));
}

void CoreAccount::onTransferStage(const QString& peer, const QString& e2eId, const QString& stage)
{
    transferStages_[e2eId] = stage;
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (m.id == 0) {
        return;
    }
    const QString opId = transferOpId(m);
    if (!log_.has(opId)) {
        log_.begin(opId, m.outgoing ? QStringLiteral("file-up") : QStringLiteral("file-down"),
            m.attName.isEmpty() ? tr("file") : m.attName, stage, peer, e2eId);
    } else {
        log_.setCancelId(opId, e2eId);
    }
    log_.update(opId, stage);
}

void CoreAccount::onServedProgress(
    const QString& peer, const QString& e2eId, const qint64 sent, const qint64 total)
{
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (m.id == 0) {
        return;
    }
    log_.update(transferOpId(m), transferStages_.value(e2eId),
        total > 0 ? humanBytes(sent) + QStringLiteral(" / ") + humanBytes(total) : QString(),
        fractionOf(sent, total, kProgressUnknown));
}

void CoreAccount::onServedFinished(
    const QString& peer, const QString& e2eId, const bool ok, const QString& error)
{
    transferStages_.remove(e2eId);
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (!ok && (m.id == 0 || peer != viewedPeer_)) {
        emit events_.actionFailed(error);
    }
    if (m.id == 0) {
        return;
    }
    if (!m.outgoing) {
        onDownloadFinished(m.id, ok, error);
        return;
    }
    log_.finish(transferOpId(m), ok, ok ? tr("Transferred") : error);
}

void CoreAccount::onDownloadFinished(const qint64 token, const bool ok, const QString& error)
{
    const QString path = pendingSavePath_.take(token);
    log_.finish(downloadOpId(token), ok, ok ? tr("Saved") : tr("Failed: %1").arg(error));
    if (!ok || path.isEmpty()) {
        return;
    }
    if (!store_.setSavedPath(token, path)) {
        reportUnsaved();
        return;
    }
    emit events_.rowChanged(store_.messageById(token).peer, token);
}

void CoreAccount::compact()
{
    const qint64 before = store_.footprint().fileBytes;
    QString reason;
    const bool rebuilt = store_.rebuild(reason);
    emit events_.storageWorkDone();
    if (!rebuilt) {
        emit events_.actionFailed(tr("The database was not compacted: %1").arg(reason));
        return;
    }
    const qint64 after = store_.footprint().fileBytes;
    emit events_.actionOk(tr("The database was compacted. %1 came back to the disk")
                              .arg(humanBytes(std::max<qint64>(0, before - after))));
}

void CoreAccount::trim(const QString& peer, const int keep)
{
    qint64 removed = 0;
    try {
        removed = peer.isEmpty() ? store_.pruneEveryChatToLatest(keep)
                                 : store_.pruneToLatest(peer, keep);
    } catch (const std::exception& error) {
        emit events_.storageWorkDone();
        emit events_.actionFailed(QString::fromUtf8(error.what()));
        return;
    }
    QString reason;
    const bool rebuilt = store_.rebuild(reason);
    emit events_.storageWorkDone();
    if (rebuilt) {
        emit events_.actionOk(tr("Removed %1 messages and compacted the database").arg(removed));
        return;
    }
    emit events_.actionFailed(tr("Removed %1 messages, but the space has not been returned to "
                                 "the disk: %2. Trimming again returns it.")
            .arg(removed)
            .arg(reason));
}

}  // namespace bazarish::app
