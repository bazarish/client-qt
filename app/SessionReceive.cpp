// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"

#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "DeliveryStatus.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QByteArray>
#include <chrono>
#include <cstring>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
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

namespace {
struct Placement {
    qint64 displayTs = 0;
    qint64 orderKey = 0;
};
Placement placeReceived(const qint64 sentAtMs, const qint64 arrivalMs)
{
    const qint64 written = sentAtMs > 0 ? sentAtMs : arrivalMs;
    return {written, written};
}
}  // namespace

void SessionController::ackAfterReceive(const QVariantMap& message)
{
    const QString pendingId = message.value("pendingId").toString();
    if (!pendingId.isEmpty()) {
        emit requestAckPending(pendingId);
    }
}

void SessionController::onMessageReceived(const QVariantMap& message)
{
    const QString peer = message.value("peer").toString();
    const QString type = message.value("type").toString();
    const QString incomingId = message.value("e2eId").toString();

    if (!incomingId.isEmpty() && store_.idForIncomingE2e(incomingId, peer) != 0) {
        return;
    }
    if (!incomingId.isEmpty() && message.value("sentByUs").toBool()
        && store_.idForE2e(incomingId) != 0) {
        return;
    }

    if (type.startsWith(QStringLiteral("call."))) {
        return;
    }

    if (type == QStringLiteral("contact.request") && !message.value("sentByUs").toBool()
        && store_.oldestOfType(peer, type, /*outgoing=*/false) != 0) {
        bazarish::log::info("a second contact request from {} keeps the plate it already has",
            peer.toStdString());
        return;
    }

    if (type == "device.chat-clear") {
        const QString cleared = message.value("ref").toString();
        if (!cleared.isEmpty()) {
            store_.clearPeer(cleared);
            if (activePeer_ == cleared) {
                loadLatestWindow();
            }
            contacts_.touch(cleared, peerName(cleared), chatPreview(cleared),
                store_.lastTime(cleared), false);
            contacts_.setUnread(cleared, store_.unreadCount(cleared));
            refreshUnreadTotal();
        }
        return;
    }

    if (type == "device.account-name") {
        const QString name = message.value("text").toString();
        if (!name.isEmpty() && name != displayName_) {
            displayName_ = name;
            emit identityChanged();
        }
        return;
    }
    if (type == "device.account-prefs") {
        emit requestEmitSettings();
        return;
    }

    if (type == "device.saved-clear") {
        store_.clearPeer(savedPeer());
        if (activePeer_ == savedPeer()) {
            loadLatestWindow();
        }
        contacts_.touch(savedPeer(), savedChatName(), QString(), nowMillis(), false);
        rebuildChatList();
        return;
    }

    if (type == "device.contact-remove") {
        const QString gone = message.value("ref").toString();
        if (!gone.isEmpty()) {
            store_.forgetPeer(gone);
            contacts_.remove(gone);
            if (activePeer_ == gone) {
                openConversation({});
            }
            rebuildChatList();
            refreshUnreadTotal();
        }
        return;
    }

    if (type == "device.contact-block" || type == "device.contact-prefs"
        || type == "device.contact-accepted") {
        ++contactsRevision_;
        emit contactsRevisionChanged();
        return;
    }

    if (type == "device.read") {
        const QString readPeer = message.value("ref").toString();
        const qint64 through = message.value("text").toString().toLongLong();
        if (!readPeer.isEmpty() && through > 0) {
            store_.applyReadThrough(readPeer, through);
            lastReadAckedId_[readPeer]
                = qMax(lastReadAckedId_.value(readPeer, 0), store_.lastReadId(readPeer));
            contacts_.setUnread(readPeer, store_.unreadCount(readPeer));
            refreshUnreadTotal();
        }
        return;
    }

    if (type == "device.chat-pin") {
        const QString pinPeer = message.value("ref").toString();
        if (!pinPeer.isEmpty()) {
            store_.setPinned(pinPeer, message.value("text").toString() == QStringLiteral("1"));
            rebuildChatList();
        }
        return;
    }

    if (type == "receipt") {
        const QString ref = message.value("ref").toString();
        const qint64 localId = store_.idForE2e(ref);
        if (localId == 0) {
            bazarish::log::info("receipt for {} arrived before the message it is about",
                ref.toStdString());
            receiptsAhead_[peer].insert(ref);
            return;
        }
        bazarish::log::info("receipt for {} applied to row {}", ref.toStdString(), localId);
        if (localId != 0) {
            markOutgoingRead(peer, localId);
        }
        return;
    }

    if (type == "reaction") {
        const bool ours = message.value("sentByUs").toBool();
        const QString reactor = ours ? fingerprint_ : peer;
        const QString target = message.value("ref").toString();
        const QString emoji = message.value("text").toString();
        store_.setReaction(peer, target, reactor, emoji);
        if (!ours && !emoji.isEmpty()) {
            noteReactionToFlash(peer, target);
            if (contactNotifications(peer)) {
                emit reactionNotification(peer, peerName(peer), emoji);
            }
        }
        ++reactionsRevision_;
        emit reactionsRevisionChanged();
        return;
    }

    if (type == "edit") {
        const qint64 localId = message.value("sentByUs").toBool()
            ? store_.idForE2e(message.value("ref").toString())
            : store_.idForIncomingE2e(message.value("ref").toString(), peer);
        if (localId != 0) {
            const QString newText = message.value("text").toString();
            const QString newKeyboard = message.value("keyboard").toString();
            store_.editContent(localId, newText, newKeyboard);
            if (peer == activePeer_) {
                conversation_.editById(localId, newText, newKeyboard);
            }
            if (peer == activePeer_ && sendReceipts_
                && localId <= lastReadAckedId_.value(peer, 0)) {
                emit requestSendReceipt(peer, message.value("ref").toString());
            }
            QString preview = newText;
            if (preview.isEmpty() && !newKeyboard.isEmpty()) {
                preview = tr("[interactive]");
            }
            contacts_.touch(peer, peerName(peer), preview, nowMillis(), false);
        }
        return;
    }

    if (type == "delete") {
        const qint64 localId = message.value("sentByUs").toBool()
            ? store_.idForE2e(message.value("ref").toString())
            : store_.idForIncomingE2e(message.value("ref").toString(), peer);
        if (localId != 0) {
            store_.removeById(localId);
            if (peer == activePeer_) {
                conversation_.removeById(localId);
            }
            contacts_.touch(peer, peerName(peer), chatPreview(peer), store_.lastTime(peer), false);
        }
        return;
    }

    if (type == "chat.clear") {
        store_.clearPeer(peer);
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;
        sys.type = QStringLiteral("system");
        sys.text = message.value("sentByUs").toBool()
            ? tr("You cleared the chat for everyone.")
            : tr("%1 cleared the chat.").arg(peerName(peer));
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        if (peer == activePeer_) {
            loadLatestWindow();
        }
        contacts_.touch(peer, peerName(peer), sys.text, sys.ts, false);
        contacts_.setUnread(peer, store_.unreadCount(peer));
        refreshUnreadTotal();
        return;
    }

    if (type == "contact.accept") {
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;
        sys.type = QStringLiteral("system");
        sys.text = tr("%1 accepted your contact request.").arg(peerName(peer));
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        showInActiveView(sys, false);
        contacts_.touch(peer, peerName(peer), sys.text, sys.ts, false);
        contacts_.setUnread(peer, store_.unreadCount(peer));
        return;
    }

    if (message.value("text").toString().isEmpty()
        && message.value("attName").toString().isEmpty()
        && message.value("attRef").toString().isEmpty()
        && message.value("attSize").toLongLong() <= 0
        && message.value("keyboard").toString().isEmpty()) {
        bazarish::log::info("silent control message ({}) not shown", type.toStdString());
        return;
    }

    StoredMessage m;
    m.peer = peer;
    m.outgoing = message.value("sentByUs").toBool();
    m.type = type;
    m.e2eId = message.value("e2eId").toString();
    m.text = message.value("text").toString();
    m.replyTo = message.value("replyTo").toString();
    m.forwarded = message.value("forwarded").toBool();
    m.attName = message.value("attName").toString();
    m.attMime = message.value("attMime").toString();
    m.attSize = message.value("attSize").toLongLong();
    m.attDurationMs = message.value("attDurationMs").toLongLong();
    m.attWave = message.value("attWave").toString();
    m.attRef = message.value("attRef").toString();
    m.keyboard = message.value("keyboard").toString();
    const Placement placement = placeReceived(message.value("sentAt").toLongLong(), nowMillis());
    m.ts = placement.displayTs;
    m.orderKey = placement.orderKey;
    m.status = m.outgoing
        ? (isSavedChat(peer) ? DeliveryStatus::Delivered : DeliveryStatus::AtRecipientServer)
        : DeliveryStatus::Received;
    m.id = store_.append(m);
    if (m.outgoing && !m.e2eId.isEmpty() && receiptsAhead_.value(peer).contains(m.e2eId)) {
        receiptsAhead_[peer].remove(m.e2eId);
        markOutgoingRead(peer, m.id);
    }

    showInActiveView(m, false);
    if (m.type == QStringLiteral("image") && !m.e2eId.isEmpty()) {
        pictureOwners_.insert(m.e2eId, m.id);
        const bool drawable = PictureStore::instance().put(
            m.e2eId, store_.media(QStringLiteral("picture:") + m.e2eId));
        store_.setHasPicture(m.id, drawable);
        conversation_.setPictureReadyForId(m.id, drawable);
    }
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = attachmentLabel(type) + QLatin1Char(' ') + m.attName;
    }
    contacts_.touch(peer, peerName(peer), preview, m.ts, false);
    contacts_.setUnread(peer, store_.unreadCount(peer));
    if (!m.outgoing && contactNotifications(peer)) {
        emit messageNotification(peer, peerName(peer));
    }
}

void SessionController::onAvatarReady(const QString& fingerprint, const QByteArray& data)
{
    AvatarStore::instance().put(fingerprint, data);
    if (fingerprint == fingerprint_) {
        avatarBusy_ = false;
        emit avatarChanged();
    }
}

}  // namespace bazarish::app
