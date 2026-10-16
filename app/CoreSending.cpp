// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "DeliveryStatus.hpp"
#include "OutboundCourier.hpp"
#include "SessionShared.hpp"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

namespace bazarish::app {

namespace {

bool isRetryPhase(const QString& phase)
{
    return phase.startsWith(QLatin1StringView(bazarish::client::kPhaseRetryPrefix));
}

QString humanDeliveryPhase(const QString& phase)
{
    if (phase == QLatin1StringView(bazarish::client::kPhasePreparing)) {
        return CoreAccount::tr("Preparing an address to send from…");
    }
    if (phase == QLatin1StringView(bazarish::client::kPhaseDialing)) {
        return CoreAccount::tr("Reaching the recipient's server…");
    }
    if (phase == QLatin1StringView(bazarish::client::kPhaseSending)) {
        return CoreAccount::tr("Sending over I2P…");
    }
    if (isRetryPhase(phase)) {
        const QLatin1StringView prefix(bazarish::client::kPhaseRetryPrefix);
        const QStringList parts = phase.sliced(prefix.size()).trimmed().split(QChar('/'));
        if (parts.size() == 2) {
            return CoreAccount::tr("Trying again (%1 of %2)…").arg(parts.at(0), parts.at(1));
        }
    }
    return phase;
}

QString sendOpId(const qint64 localId)
{
    return QStringLiteral("send:") + QString::number(localId);
}

StoredMessage outgoing(const QString& peer, const QString& type, const QString& e2eId,
    const QString& replyTo, const int status)
{
    StoredMessage m;
    m.peer = peer;
    m.outgoing = true;
    m.type = type;
    m.e2eId = e2eId;
    m.replyTo = replyTo;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = status;
    return m;
}

}  // namespace

bool CoreAccount::isSavedChat(const QString& peer) const
{
    return !peer.isEmpty() && peer == contacts_.ownFingerprint();
}

void CoreAccount::sendText(
    const QString& peer, const QString& text, const QString& replyTo, const QString& e2eId)
{
    const bool saved = isSavedChat(peer);
    StoredMessage m = outgoing(peer, QStringLiteral("text"), e2eId, replyTo,
        saved ? DeliveryStatus::Delivering : DeliveryStatus::Preparing);
    m.text = text;
    m.id = store_.append(m);
    if (m.id == 0) {
        reportUnsaved();
        return;
    }
    statusById_[m.id] = m.status;
    if (saved) {
        savedSends_.insert(m.id);
    }
    emit events_.rowAdded(peer, m.id, true);
    if (!saved) {
        log_.begin(sendOpId(m.id), QStringLiteral("send"),
            tr("To %1").arg(contacts_.peerName(peer)), tr("Sending…"), peer);
    }
    emit commands_.requestSendText(peer, text, m.id, e2eId, replyTo);
}

qint64 CoreAccount::beginAttachmentSend(StoredMessage m)
{
    m.id = store_.append(m);
    if (m.id == 0) {
        reportUnsaved();
        return 0;
    }
    statusById_[m.id] = m.status;
    emit events_.rowAdded(m.peer, m.id, true);
    log_.begin(sendOpId(m.id), QStringLiteral("file-up"), m.attName, tr("Sending…"), m.peer);
    return m.id;
}

void CoreAccount::sendFile(
    const QString& peer, const QString& path, const QString& replyTo, const QString& e2eId)
{
    const QFileInfo info(path);
    StoredMessage m = outgoing(peer, QStringLiteral("file"), e2eId, replyTo,
        isSavedChat(peer) ? DeliveryStatus::Delivering : DeliveryStatus::Preparing);
    m.attName = info.fileName();
    m.attSize = info.size();
    m.attMime = QMimeDatabase().mimeTypeForFile(info).name();
    m.attSrcPath = path;
    const qint64 id = beginAttachmentSend(m);
    if (id != 0) {
        emit commands_.requestSendFile(peer, path, id, e2eId, replyTo);
    }
}

void CoreAccount::sendPicture(const QString& peer, const QByteArray& bytes, const QString& name,
    const QString& mime, const QString& replyTo, const QString& e2eId)
{
    StoredMessage m = outgoing(peer, QStringLiteral("image"), e2eId, replyTo,
        isSavedChat(peer) ? DeliveryStatus::Delivering : DeliveryStatus::Preparing);
    m.attName = name;
    m.attSize = bytes.size();
    m.attMime = mime;
    m.hasPicture = true;
    const qint64 id = beginAttachmentSend(m);
    if (id == 0) {
        return;
    }
    if (!store_.setHasPicture(id, true)) {
        reportUnsaved();
    }
    emit commands_.requestSendPicture(peer, bytes, name, mime, id, e2eId, replyTo);
}

void CoreAccount::sendVoice(const QString& peer, const QByteArray& opus, const qint64 durationMs,
    const QString& wave, const QString& replyTo, const QString& e2eId)
{
    StoredMessage m = outgoing(peer, QStringLiteral("audio"), e2eId, replyTo,
        isSavedChat(peer) ? DeliveryStatus::Delivering : DeliveryStatus::Preparing);
    m.attMime = QStringLiteral("audio/opus");
    m.attSize = opus.size();
    m.attDurationMs = durationMs;
    m.attWave = wave;
    m.id = store_.append(m);
    if (m.id == 0) {
        reportUnsaved();
        return;
    }
    statusById_[m.id] = m.status;
    emit events_.rowAdded(peer, m.id, true);
    log_.begin(sendOpId(m.id), QStringLiteral("send"), tr("To %1").arg(contacts_.peerName(peer)),
        tr("Sending…"), peer);
    emit commands_.requestSendVoice(peer, opus, durationMs, m.id, e2eId, replyTo);
}

void CoreAccount::forwardMessage(
    const QString& fromPeer, const QString& e2eId, const QString& toPeer, const QString& newE2eId)
{
    const StoredMessage source = store_.messageByE2e(e2eId, fromPeer);
    if (source.id == 0) {
        return;
    }
    StoredMessage m
        = outgoing(toPeer, source.type, newE2eId, QString(), DeliveryStatus::Preparing);
    m.text = source.text;
    m.forwarded = true;
    QByteArray media;
    QString path;
    QString preview;
    if (source.type == QStringLiteral("audio")) {
        media = store_.media(QStringLiteral("voice:") + source.e2eId);
        if (media.isEmpty()) {
            emit events_.actionFailed(tr("this voice message is no longer on this device"));
            return;
        }
        m.attMime = source.attMime;
        m.attSize = media.size();
        m.attDurationMs = source.attDurationMs;
        m.attWave = source.attWave;
        preview = tr("[voice]");
    } else if (source.type != QStringLiteral("text")) {
        m.attName = source.attName;
        m.attMime = source.attMime;
        m.attSize = source.attSize;
        if (source.type == QStringLiteral("image")) {
            media = store_.media(QStringLiteral("picture:") + source.e2eId);
            if (media.isEmpty() && !source.savedPath.isEmpty()) {
                QFile saved(source.savedPath);
                if (saved.open(QIODevice::ReadOnly)) {
                    media = saved.readAll();
                }
            }
            if (media.isEmpty()) {
                emit events_.actionFailed(
                    tr("save this to your device first, then it can be forwarded"));
                return;
            }
            m.hasPicture = true;
        } else {
            path = source.savedPath;
            if (path.isEmpty() || !QFileInfo::exists(path)) {
                emit events_.actionFailed(
                    tr("save this to your device first, then it can be forwarded"));
                return;
            }
            m.attSrcPath = path;
        }
    }
    m.id = store_.append(m);
    if (m.id == 0) {
        reportUnsaved();
        return;
    }
    if (m.hasPicture && !store_.setHasPicture(m.id, true)) {
        reportUnsaved();
    }
    statusById_[m.id] = DeliveryStatus::Preparing;
    const bool saved = isSavedChat(toPeer);
    if (saved) {
        savedSends_.insert(m.id);
    }
    emit events_.rowAdded(toPeer, m.id, true);
    if (!saved) {
        log_.begin(sendOpId(m.id), QStringLiteral("send"),
            tr("To %1").arg(contacts_.peerName(toPeer)), tr("Forwarding…"), toPeer);
    }
    if (source.type == QStringLiteral("audio")) {
        emit commands_.requestSendVoice(
            toPeer, media, m.attDurationMs, m.id, m.e2eId, QString(), true);
    } else if (source.type == QStringLiteral("text")) {
        emit commands_.requestSendText(toPeer, m.text, m.id, m.e2eId, QString(), true);
    } else if (source.type == QStringLiteral("image")) {
        emit commands_.requestSendPicture(
            toPeer, media, m.attName, m.attMime, m.id, m.e2eId, QString(), true);
    } else {
        emit commands_.requestSendFile(toPeer, path, m.id, m.e2eId, QString(), true);
    }
}

void CoreAccount::resend(const QString& peer, const qint64 localId)
{
    const StoredMessage stored = store_.messageById(localId);
    if (stored.id == 0 || stored.peer != peer) {
        return;
    }
    QByteArray media;
    if (stored.type == QStringLiteral("file")) {
        if (stored.attSrcPath.isEmpty() || !QFileInfo::exists(stored.attSrcPath)) {
            emit events_.resendFilePickRequested();
            return;
        }
    } else if (stored.type == QStringLiteral("audio")) {
        media = store_.media(QStringLiteral("voice:") + stored.e2eId);
        if (media.isEmpty()) {
            emit events_.actionFailed(tr("this voice message is no longer on this device"));
            return;
        }
    } else if (stored.type == QStringLiteral("image")) {
        media = store_.media(QStringLiteral("picture:") + stored.e2eId);
        if (media.isEmpty()) {
            emit events_.actionFailed(tr("this picture is no longer on this device"));
            return;
        }
    }
    restartDelivery(peer, localId);
    const bool attachment
        = stored.type == QStringLiteral("file") || stored.type == QStringLiteral("image");
    QString title = tr("To %1").arg(contacts_.peerName(peer));
    if (stored.type == QStringLiteral("file")) {
        title = stored.attName.isEmpty() ? tr("file") : stored.attName;
    } else if (stored.type == QStringLiteral("image")) {
        title = stored.attName.isEmpty() ? tr("image") : stored.attName;
    }
    log_.begin(sendOpId(localId), attachment ? QStringLiteral("file-up") : QStringLiteral("send"),
        title, tr("Sending again…"), peer);
    if (stored.type == QStringLiteral("file")) {
        emit commands_.requestSendFile(peer, stored.attSrcPath, localId, stored.e2eId,
            stored.replyTo, stored.forwarded);
    } else if (stored.type == QStringLiteral("audio")) {
        emit commands_.requestSendVoice(peer, media, stored.attDurationMs, localId, stored.e2eId,
            stored.replyTo, stored.forwarded);
    } else if (stored.type == QStringLiteral("image")) {
        emit commands_.requestSendPicture(peer, media, stored.attName, stored.attMime, localId,
            stored.e2eId, stored.replyTo, stored.forwarded);
    } else {
        emit commands_.requestSendText(
            peer, stored.text, localId, stored.e2eId, stored.replyTo, stored.forwarded);
    }
}

void CoreAccount::editMessage(
    const QString& peer, const qint64 localId, const QString& e2eId, const QString& text)
{
    if (!store_.editContent(localId, text, {})) {
        reportUnsaved();
        return;
    }
    restartDelivery(peer, localId);
    emit commands_.requestSendEdit(peer, e2eId, localId, text);
}

void CoreAccount::deleteMessage(
    const QString& peer, const qint64 localId, const QString& e2eId, const bool outgoing)
{
    if (!store_.removeById(localId)) {
        reportUnsaved();
        return;
    }
    if (!e2eId.isEmpty()) {
        emit commands_.requestUnsend(e2eId);
    }
    statusById_.remove(localId);
    emit events_.rowRemoved(peer, localId);
    if (outgoing && !e2eId.isEmpty()) {
        emit commands_.requestSendDelete(peer, e2eId);
    }
}

void CoreAccount::restartDelivery(const QString& peer, const qint64 localId)
{
    statusById_[localId] = DeliveryStatus::Preparing;
    if (!store_.updateStatus(localId, DeliveryStatus::Preparing)) {
        reportUnsaved();
    }
    emit events_.rowChanged(peer, localId);
    emit events_.messageError(localId, {});
}

void CoreAccount::bumpStatus(const qint64 localId, const int status)
{
    const int current = statusById_.value(localId, DeliveryStatus::Preparing);
    if (status != DeliveryStatus::Failed && status <= current) {
        return;
    }
    statusById_[localId] = status;
    if (!store_.updateStatus(localId, status)) {
        reportUnsaved();
    }
    const StoredMessage row = store_.messageById(localId);
    emit events_.rowChanged(row.peer, localId);
}

void CoreAccount::onSendProgress(const qint64 localId, const int state)
{
    if (state == DeliveryStatus::AtRecipientServer && savedSends_.contains(localId)) {
        bumpStatus(localId, DeliveryStatus::Delivered);
        log_.finish(sendOpId(localId), true, tr("Saved"));
        return;
    }
    bumpStatus(localId, state);
    if (state == DeliveryStatus::AtRecipientServer) {
        log_.finish(sendOpId(localId), true, tr("Handed to the recipient's server"));
    }
}

void CoreAccount::onSendResult(const qint64 localId, const bool ok, const QString& error)
{
    if (ok) {
        emit events_.messageError(localId, {});
        if (savedSends_.remove(localId)) {
            bumpStatus(localId, DeliveryStatus::Delivered);
        }
        return;
    }
    savedSends_.remove(localId);
    bumpStatus(localId, DeliveryStatus::Failed);
    emit events_.messageError(localId, error);
    log_.finish(sendOpId(localId), false, tr("Failed: %1").arg(error));
}

void CoreAccount::onSendPhase(const qint64 localId, const QString& phase)
{
    const QString human = humanDeliveryPhase(phase);
    log_.update(sendOpId(localId), human);
    if (isRetryPhase(phase)) {
        emit events_.messageError(localId, human);
    }
}

void CoreAccount::noteDeliveredThrough(const QString& peer, const qint64 uptoId)
{
    for (auto it = statusById_.begin(); it != statusById_.end(); ++it) {
        if (it.key() <= uptoId) {
            const StoredMessage row = store_.messageById(it.key());
            if (row.peer == peer) {
                it.value() = row.status;
            }
        }
    }
}

}  // namespace bazarish::app
