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

// Qt makes `emit` a macro and the log header declares a function of that name,
// so the keyword is stood down for the length of this include.
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
// When a received message was written, and where it therefore sits. Both are the
// sender's own sentAt, taken from inside the sealed envelope: nothing a server
// says about a message decides where it goes, because the server is not told
// anything about the message to say (docs-main Messages.md "Ordering and
// timestamps"). The local clock stands in only for an envelope that carries no
// time at all, which nothing this client sends does.
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

// What arrives in the mailbox, and where each kind of item goes.

void SessionController::ackAfterReceive(const QVariantMap& message)
{
    // Runs after onMessageReceived (connected later to the same signal), so the item
    // has already been durably stored/handled. Only now do we ack it on the server,
    // closing the ack-before-store window that lost messages on a restart.
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

    // Idempotent receive, before anything acts on the message. The mailbox is
    // at-least-once: an item whose ack was lost, or that a sender retried, is
    // legitimately re-offered and arrives here again with the same id. Dedup
    // against the transcript - if this conversation already holds an incoming
    // message or note with this id, this is that redelivery. It has to come first:
    // the handlers below return early, and a note stored by one of them (a contact
    // request agreed to, a cleared chat) would otherwise be written once per
    // redelivery. (A read receipt is only sent on a real read, handled by
    // markReadThroughRow.)
    if (!incomingId.isEmpty() && store_.idForIncomingE2e(incomingId, peer) != 0) {
        return;
    }
    // The same, for what another device of ours sent: an echo names the message
    // it echoes, so a second copy of it - a redelivery, or a message this device
    // wrote and then heard about - is that message, not another one. Without
    // this the conversation grew a second bubble for one thing that was sent
    // once.
    if (!incomingId.isEmpty() && message.value("sentByUs").toBool()
        && store_.idForE2e(incomingId) != 0) {
        return;
    }

    // Call signalling drives the call screen via callStateChanged, never the
    // chat list.
    if (type.startsWith(QStringLiteral("call."))) {
        return;
    }

    // One invitation per conversation. A request that is sent again - the same
    // one repeated, or a fresh one after the peer removed us - is the same
    // invitation, and a chat that grows a second plate for it reads as two people
    // asking. What it carries (their routing, the pass they hand over) has already been
    // applied by the core; only the plate is dropped.
    if (type == QStringLiteral("contact.request") && !message.value("sentByUs").toBool()
        && store_.oldestOfType(peer, type, /*outgoing=*/false) != 0) {
        bazarish::log::info("a second contact request from {} keeps the plate it already has",
            peer.toStdString());
        return;
    }

    // Another device of ours emptied its copy of a conversation.
    if (type == "device.chat-clear") {
        const QString cleared = message.value("ref").toString();
        if (!cleared.isEmpty()) {
            store_.clearPeer(cleared);
            if (activePeer_ == cleared) {
                loadLatestWindow();
            }
            contacts_.touch(cleared, peerName(cleared), store_.lastText(cleared),
                store_.lastTime(cleared), false);
            contacts_.setUnread(cleared, store_.unreadCount(cleared));
            refreshUnreadTotal();
        }
        return;
    }

    // The account was renamed, or an account-wide answer changed, on another
    // device of ours. The core has applied it; the window catches up.
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

    // Another device of ours emptied the saved chat. Nothing is announced: this is
    // the user's own action arriving late.
    if (type == "device.saved-clear") {
        store_.clearPeer(savedPeer());
        if (activePeer_ == savedPeer()) {
            loadLatestWindow();
        }
        contacts_.touch(savedPeer(), savedChatName(), QString(), nowMillis(), false);
        rebuildChatList();
        return;
    }

    // Another device of ours removed a contact: the core has already dropped it
    // here, so what is left is the conversation and the row it sat in.
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

    // A block, a per-contact switch, or a contact request agreed to on another
    // device. The core applied it; the interface only re-reads what it shows -
    // which is what takes the Agree button off a request already answered.
    if (type == "device.contact-block" || type == "device.contact-prefs"
        || type == "device.contact-accepted") {
        ++contactsRevision_;
        emit contactsRevisionChanged();
        return;
    }

    // A pin/unpin synced from another of our devices (ref = the pinned chat, text =
    // "1"/"0"): apply it to the local pin list and re-sort. Silent - no bubble.
    if (type == "device.read") {
        // Another device of ours has read this conversation. What it read is
        // already here or it is not; either way nothing else changes.
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

    // A read receipt: the peer read our referenced message (the green state), and
    // by the read high-water everything we sent them before it too. Not shown.
    if (type == "receipt") {
        const QString ref = message.value("ref").toString();
        const qint64 localId = store_.idForE2e(ref);
        if (localId == 0) {
            // The receipt outran the message it is about. On a second device the
            // message arrives as an echo of what the first device sent, and
            // nothing promises the mailbox hands the two over in that order:
            // dropping the receipt here left the bubble amber for good. Kept, and
            // applied when the message lands.
            bazarish::log::info("receipt for {} arrived before the message it is about",
                ref.toStdString());
            receiptsAhead_[peer].insert(ref);
            return;
        }
        // Said out loud on both sides: a receipt reaches an account, and every
        // device of it takes its own copy from the mailbox. When one device shows
        // green while another stays amber, the two lines are what separate a
        // receipt that arrived late from one that arrived and was not applied.
        bazarish::log::info("receipt for {} applied to row {}", ref.toStdString(), localId);
        if (localId != 0) {
            markOutgoingRead(peer, localId);
        }
        return;
    }

    // A reaction: record the reactor's emoji against the target message and
    // re-drive the chips. Never a chat bubble. The reactor is the peer who sent it.
    if (type == "reaction") {
        // Whose reaction it is: the peer's, or ours when this is another device of
        // ours saying what we did there.
        const bool ours = message.value("sentByUs").toBool();
        const QString reactor = ours ? fingerprint_ : peer;
        const QString target = message.value("ref").toString();
        const QString emoji = message.value("text").toString();
        store_.setReaction(peer, target, reactor, emoji);
        // Somebody put this on one of our messages. It gets a flash when the
        // conversation is next looked at, and - unless it was us, on another
        // device of ours - it is announced like an arrival, with its own sound.
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

    // An in-place edit of a message this peer previously sent us: update it
    // where it sits instead of adding a new bubble. Scoped to incoming-from-peer
    // in the store, so a peer can only edit its own messages.
    if (type == "edit") {
        // Scoped to incoming-from-peer in the store, so a peer can only edit its
        // own messages. An edit echoed from another device of ours is about a
        // message of ours, so it is looked up unscoped.
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
            // If we had already read this message, the edit is read again the moment
            // it lands in the open chat: re-acknowledge it so the sender's edited
            // bubble can advance to delivered (green).
            if (peer == activePeer_ && sendReceipts_
                && localId <= lastReadAckedId_.value(peer, 0)) {
                emit requestSendReceipt(peer, message.value("ref").toString());
            }
            QString preview = newText;
            if (preview.isEmpty() && !newKeyboard.isEmpty()) {
                preview = "[interactive]";
            }
            contacts_.touch(peer, peerName(peer), preview, nowMillis(), false);
        }
        return;
    }

    // A delete-for-everyone of a message this peer previously sent us: remove it
    // with no trace. Scoped to incoming-from-peer in the store, so a peer can only
    // delete its own messages.
    if (type == "delete") {
        // As with an edit: ours refers to a message of ours.
        const qint64 localId = message.value("sentByUs").toBool()
            ? store_.idForE2e(message.value("ref").toString())
            : store_.idForIncomingE2e(message.value("ref").toString(), peer);
        if (localId != 0) {
            store_.removeById(localId);
            if (peer == activePeer_) {
                conversation_.removeById(localId);
            }
            contacts_.touch(peer, peerName(peer), store_.lastText(peer), store_.lastTime(peer), false);
        }
        return;
    }

    // The peer cleared the whole conversation for everyone: honour their wish and
    // wipe our transcript with them, leaving a single note so the empty chat
    // explains itself.
    if (type == "chat.clear") {
        store_.clearPeer(peer);
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;  // so a redelivery is recognised as one
        sys.type = QStringLiteral("system");
        sys.text = message.value("sentByUs").toBool()
            ? QStringLiteral("You cleared the chat for everyone.")
            : peerName(peer) + QStringLiteral(" cleared the chat.");
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

    // The peer agreed to our contact request: we are now mutual contacts (their
    // descriptor already arrived via the bootstrap in sync). Surface a note.
    if (type == "contact.accept") {
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;  // so a redelivery is recognised as one
        sys.type = QStringLiteral("system");
        sys.text = peerName(peer) + QStringLiteral(" accepted your contact request.");
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        showInActiveView(sys, false);
        contacts_.touch(peer, peerName(peer), sys.text, sys.ts, false);
        contacts_.setUnread(peer, store_.unreadCount(peer));
        return;
    }

    // Control content with nothing to show: a button press, or a type a newer
    // client sends that this one cannot render, carries no text.
    // Stored, each became an empty bubble that also counted as unread. The item is
    // still acked - ackAfterReceive runs off the same signal - so it does not come
    // back.
    // Audio that rides inside the message has no name and nothing to fetch, so
    // "nothing to show" has to ask about the bytes too - without this a voice
    // message was received, acked and receipted, and then dropped here.
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
    // Another device of ours sent this; it belongs on our side of the chat.
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
    // Order by and display the sender's own sentAt (ms): a recent burst that
    // arrived out of order is reordered into place; a long-delayed arrival is
    // appended at the end as new (docs-main Messages.md "Ordering and timestamps").
    const Placement placement = placeReceived(message.value("sentAt").toLongLong(), nowMillis());
    m.ts = placement.displayTs;
    m.orderKey = placement.orderKey;
    // An echo exists because the send it echoes was stored by the recipient's
    // server - the device that sent it writes the echo only then - so it lands
    // here in the same amber state the sender is showing, and the contact's read
    // receipt (which reaches every device of this account) turns it green here
    // too. A note to the saved chat is the exception: there is no correspondent
    // to read it, so it is finished the moment our own server holds it.
    m.status = m.outgoing
        ? (isSavedChat(peer) ? DeliveryStatus::Delivered : DeliveryStatus::AtRecipientServer)
        : DeliveryStatus::Received;
    m.id = store_.append(m);
    // A receipt that arrived before this message was here has been waiting for
    // it. Applied now, so an echo of our own send does not sit amber forever.
    if (m.outgoing && !m.e2eId.isEmpty() && receiptsAhead_.value(peer).contains(m.e2eId)) {
        receiptsAhead_[peer].remove(m.e2eId);
        markOutgoingRead(peer, m.id);
    }

    showInActiveView(m, false);
    // A picture arrives inside the message, so there is nothing to fetch: the
    // core has already put it in the account, and this reads it back to draw.
    if (m.type == QStringLiteral("image") && !m.e2eId.isEmpty()) {
        pictureOwners_.insert(m.e2eId, m.id);
        // The core has just stored it; read it back through this side's own
        // connection so the bubble draws it now, not after the next sync.
        const bool drawable = PictureStore::instance().put(
            m.e2eId, store_.media(QStringLiteral("picture:") + m.e2eId));
        store_.setHasPicture(m.id, drawable);
        conversation_.setPictureReadyForId(m.id, drawable);
    }
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = "[" + type + "] " + m.attName;
    }
    contacts_.touch(peer, peerName(peer), preview, m.ts, false);
    // The unread badge is the persistent count of incoming messages past the read
    // high-water (set when messages actually scroll into view), not a running
    // increment - so it stays accurate across restarts and partial reads.
    contacts_.setUnread(peer, store_.unreadCount(peer));
    if (!m.outgoing && contactNotifications(peer)) {
        // An echo of our own message from another device is not news to anybody,
        // and neither is a contact the user has asked to keep quiet - the chat
        // list still counts it, so nothing is hidden, it only stays silent.
        emit messageNotification(peer, peerName(peer));
    }
    // No receipt is sent on arrival: the green "read" state is reported only when
    // the user actually reads the message (chat open + window focused + the message
    // in view), driven by markReadThroughRow.
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
