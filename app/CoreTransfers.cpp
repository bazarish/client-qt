// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "Background.hpp"
#include "Documents.hpp"
#include "SessionShared.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>

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

// Its own connection: the account's store stays free for the core's thread meanwhile.
StoreUpkeep keepStore(const QString& path, const QString& passphrase, const QString& peer,
    const std::optional<int> keep)
{
    StoreUpkeep done;
    TranscriptStore store;
    if (!store.open(path, passphrase)) {
        done.problem = CoreAccount::tr("This profile could not be opened");
        return done;
    }
    done.before = store.footprint().fileBytes;
    if (keep.has_value()) {
        try {
            done.removed = peer.isEmpty() ? store.pruneEveryChatToLatest(*keep)
                                          : store.pruneToLatest(peer, *keep);
        } catch (const std::exception& error) {
            done.problem = QString::fromUtf8(error.what());
            return done;
        }
    }
    done.compacted = store.compact(done.reason);
    done.after = store.footprint().fileBytes;
    return done;
}

}  // namespace

void CoreAccount::onConnectRequested()
{
    connecting_ = true;
    connectPercent_ = 0;
    log_.begin(kConnectOperationId, QStringLiteral("connect"), tr("Connecting this account"),
        tr("Starting..."));
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
    keyPresses_.insert(opId, ref);
    log_.begin(opId, QStringLiteral("bot"),
        (label.isEmpty() ? data : label) + QStringLiteral(" → ") + contacts_.peerName(peer),
        tr("Sending..."), peer);
    emit commands_.requestSendCallback(opId, peer, data, ref);
}

void CoreAccount::sendCommand(const QString& peer, const QString& command, const QString& args,
    const QString& label, const QString& keyRef)
{
    const QString opId = QStringLiteral("bot-command:") + command + QStringLiteral(":") + keyRef;
    if (!keyRef.isEmpty()) {
        keyPresses_.insert(opId, keyRef);
    }
    log_.begin(opId, QStringLiteral("bot"),
        (label.isEmpty() ? QStringLiteral("/") + command : label) + QStringLiteral(" → ")
            + contacts_.peerName(peer),
        tr("Sending..."), peer);
    emit commands_.requestSendCommand(opId, peer, command, args);
}

void CoreAccount::saveAttachment(
    const QString& peer, const QString& e2eId, const QString& dest, const qint64 token)
{
    pendingSavePath_.insert(token, dest);
    const StoredMessage message = store_.messageById(token);
    const QString name = isDocumentUri(dest) ? message.attName : QFileInfo(dest).fileName();
    if (const QByteArray carried = mediaOf(message); !carried.isEmpty()) {
        log_.begin(downloadOpId(token), QStringLiteral("file-down"), name, tr("Saving..."), peer,
            e2eId);
        try {
            const std::unique_ptr<client::FileOutput> out = openOutput(dest);
            out->write(bazarish::viewOf(std::string_view(
                carried.constData(), static_cast<std::size_t>(carried.size()))));
            out->finish();
        } catch (const std::exception& error) {
            onDownloadFinished(token, false, QString::fromUtf8(error.what()));
            return;
        }
        onDownloadFinished(token, true, {});
        return;
    }
    log_.begin(downloadOpId(token), QStringLiteral("file-down"), name,
        tr("Connecting..."), peer, e2eId);
    emit commands_.requestSaveAttachment(peer, e2eId, message.attRef, dest, token);
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
    keepStoreInBackground({}, std::nullopt);
}

void CoreAccount::trim(const QString& peer, const int keep)
{
    keepStoreInBackground(peer, keep);
}

void CoreAccount::keepStoreInBackground(const QString& peer, const std::optional<int> keep)
{
    const QPointer<CoreAccount> account(this);
    inBackground([account, path = accountPath_, passphrase = passphrase_, peer, keep]() {
        const StoreUpkeep done = keepStore(path, passphrase, peer, keep);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [account, done, trimmed = keep.has_value()]() {
                if (account) {
                    account->finishStoreUpkeep(done, trimmed);
                }
            },
            Qt::QueuedConnection);
    });
}

void CoreAccount::finishStoreUpkeep(const StoreUpkeep& done, const bool trimmed)
{
    emit events_.storageWorkDone();
    if (!trimmed) {
        if (!done.problem.isEmpty() || !done.compacted) {
            emit events_.actionFailed(tr("The database was not compacted: %1")
                    .arg(done.problem.isEmpty() ? done.reason : done.problem));
            return;
        }
        emit events_.actionOk(tr("The database was compacted. %1 came back to the disk")
                .arg(humanBytes(std::max<qint64>(0, done.before - done.after))));
        return;
    }
    if (!done.problem.isEmpty()) {
        emit events_.actionFailed(done.problem);
        return;
    }
    if (done.compacted) {
        emit events_.actionOk(
            tr("Removed %1 messages and compacted the database").arg(done.removed));
        return;
    }
    emit events_.actionFailed(tr("Removed %1 messages, but the space has not been returned to "
                                 "the disk: %2. Trimming again returns it.")
            .arg(done.removed)
            .arg(done.reason));
}

}  // namespace bazarish::app
