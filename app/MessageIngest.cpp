// Bazarish project (c) 2026
#include "MessageIngest.hpp"

#include "ContactBook.hpp"
#include "DeliveryStatus.hpp"
#include "SessionShared.hpp"
#include "SystemNotes.hpp"
#include "TranscriptStore.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

namespace bazarish::app {

namespace {
struct Placement {
    qint64 displayTs = 0;
    qint64 orderKey = 0;
};
// A row from the other side never goes above what this side had written before it came.
Placement placeReceived(const qint64 sentAtMs, const qint64 arrivalMs, const qint64 floor)
{
    const qint64 written = sentAtMs > 0 ? sentAtMs : arrivalMs;
    return {written, std::max(written, floor)};
}
}  // namespace

MessageIngest::MessageIngest(
    TranscriptStore& store, const ContactBook& contacts, QObject* const parent)
    : QObject(parent)
    , store_(store)
    , contacts_(contacts)
{
}

qint64 MessageIngest::orderFloor(const QString& peer, const QVariantMap& message) const
{
    if (message.value("sentByUs").toBool()) {
        return 0;
    }
    const qint64 own = store_.newestOwnOrderKey(peer);
    return own > 0 ? own + 1 : 0;
}

bool MessageIngest::takeIn(const QVariantMap& message)
{
    const QString peer = message.value("peer").toString();
    const QString type = message.value("type").toString();
    const QString incomingId = message.value("e2eId").toString();

    if (!incomingId.isEmpty() && store_.idForIncomingE2e(incomingId, peer) != 0) {
        return true;
    }
    if (!incomingId.isEmpty() && message.value("sentByUs").toBool()
        && store_.idForE2e(incomingId) != 0) {
        return true;
    }

    if (type.startsWith(QStringLiteral("call."))) {
        return true;
    }

    if (!message.value("sentByUs").toBool() && !noteGroupBlock(peer, message)) {
        return false;
    }
    if (type == QStringLiteral("group.state")) {
        return true;
    }
    if (type == QStringLiteral("group.reactions")) {
        return applyGroupReactions(peer, message.value("groupReactions").toString());
    }

    if (type == QStringLiteral("contact.request") && !message.value("sentByUs").toBool()
        && store_.oldestOfType(peer, type, /*outgoing=*/false) != 0) {
        bazarish::log::info("a second contact request from {} keeps the plate it already has",
            peer.toStdString());
        return true;
    }

    if (type == "device.chat-clear") {
        const QString cleared = message.value("ref").toString();
        if (!cleared.isEmpty()) {
            if (!store_.clearPeer(cleared)) {
                return false;
            }
            emit peerReset(cleared);
        }
        return true;
    }

    if (type == "device.account-name") {
        const QString name = message.value("text").toString();
        if (!name.isEmpty()) {
            emit accountRenamed(name);
        }
        return true;
    }
    if (type == "device.account-prefs") {
        emit settingsAsked();
        return true;
    }

    if (type == "device.saved-clear") {
        if (!store_.clearPeer(contacts_.ownFingerprint())) {
            return false;
        }
        emit peerReset(contacts_.ownFingerprint());
        return true;
    }

    if (type == "device.contact-remove") {
        const QString gone = message.value("ref").toString();
        if (!gone.isEmpty()) {
            if (!store_.forgetPeer(gone)) {
                return false;
            }
            emit peerForgotten(gone);
        }
        return true;
    }

    if (type == "device.contact-block" || type == "device.contact-prefs"
        || type == "device.contact-accepted" || type == "device.contact") {
        emit contactsRevised();
        return true;
    }

    if (type == "device.read") {
        const QString readPeer = message.value("ref").toString();
        const qint64 through = message.value("text").toString().toLongLong();
        if (!readPeer.isEmpty() && through > 0) {
            if (!store_.applyReadThrough(readPeer, through)) {
                return false;
            }
            emit readElsewhere(readPeer);
        }
        return true;
    }

    if (type == "device.chat-pin") {
        const QString pinPeer = message.value("ref").toString();
        if (!pinPeer.isEmpty()) {
            if (!store_.setPinned(
                    pinPeer, message.value("text").toString() == QStringLiteral("1"))) {
                return false;
            }
            emit chatsChanged();
        }
        return true;
    }

    if (type == "receipt") {
        const QString ref = message.value("ref").toString();
        const qint64 localId = store_.idForE2e(ref);
        if (localId == 0) {
            bazarish::log::info("receipt for {} arrived before the message it is about",
                ref.toStdString());
            receiptsAhead_[peer].insert(ref);
            return true;
        }
        bazarish::log::info("receipt for {} applied to row {}", ref.toStdString(), localId);
        return markOutgoingRead(peer, localId);
    }

    if (type == "reaction") {
        const bool ours = message.value("sentByUs").toBool();
        const QString reactor = ours ? contacts_.ownFingerprint() : peer;
        const QString target = message.value("ref").toString();
        const QString emoji = message.value("text").toString();
        if (!store_.setReaction(peer, target, reactor, emoji)) {
            return false;
        }
        if (!ours && !emoji.isEmpty()) {
            emit reactionArrived(peer, target, emoji);
        }
        emit reactionsChanged();
        return true;
    }

    if (type == "edit") {
        const qint64 localId = message.value("sentByUs").toBool()
            ? store_.idForE2e(message.value("ref").toString())
            : store_.idForIncomingE2e(message.value("ref").toString(), peer);
        if (localId != 0) {
            const QString newText = message.value("text").toString();
            const QString newKeyboard = message.value("keyboard").toString();
            if (!store_.editContent(localId, newText, newKeyboard)) {
                return false;
            }
            emit rowChanged(peer, localId);
            emit editArrived(peer, localId, message.value("ref").toString());
        }
        return true;
    }

    if (type == "delete") {
        const qint64 localId = message.value("sentByUs").toBool()
            ? store_.idForE2e(message.value("ref").toString())
            : store_.idForIncomingE2e(message.value("ref").toString(), peer);
        if (localId != 0) {
            if (!store_.removeById(localId)) {
                return false;
            }
            emit rowRemoved(peer, localId);
        }
        return true;
    }

    if (type == "chat.clear") {
        if (!store_.clearPeer(peer)) {
            return false;
        }
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;
        sys.type = QStringLiteral("system");
        sys.text = message.value("sentByUs").toBool()
            ? encodeSystemNote(QT_TR_NOOP("You cleared the chat for everyone."))
            : encodeSystemNote(QT_TR_NOOP("%1 cleared the chat."), {contacts_.peerName(peer)});
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        if (sys.id == 0) {
            return false;
        }
        emit peerReset(peer);
        return true;
    }

    if (type == "bot.menu") {
        if (!store_.setBotMenu(peer, message.value("menuItems").toString())) {
            return false;
        }
        emit botMenuChanged(peer);
        return true;
    }

    if (type == "chat.timer") {
        if (store_.groupState(peer).has_value()) {
            bazarish::log::info("an auto-delete timer from a group host ignored");
            return true;
        }
        const qint64 seconds = message.value("timerSeconds").toLongLong();
        if (!store_.setChatTimer(peer, seconds)) {
            return false;
        }
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;
        sys.type = QStringLiteral("system");
        sys.text = chatTimerNote(
            seconds, message.value("sentByUs").toBool(), contacts_.peerName(peer));
        const Placement placement = placeReceived(
            message.value("sentAt").toLongLong(), nowMillis(), orderFloor(peer, message));
        sys.ts = placement.displayTs;
        sys.orderKey = placement.orderKey;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        if (sys.id == 0) {
            return false;
        }
        emit chatTimerChanged(peer, seconds);
        emit rowAdded(peer, sys.id, message.value("sentByUs").toBool());
        return true;
    }

    if (type == "contact.accept") {
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;
        sys.type = QStringLiteral("system");
        sys.text = encodeSystemNote(
            QT_TR_NOOP("%1 accepted your contact request."), {contacts_.peerName(peer)});
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        if (sys.id == 0) {
            return false;
        }
        emit rowAdded(peer, sys.id, false);
        const qint64 request
            = store_.oldestOfType(peer, QStringLiteral("contact.request"), /*outgoing=*/true);
        return request == 0 || markOutgoingRead(peer, request);
    }

    if (type == "unsupported") {
        StoredMessage sys;
        sys.peer = peer;
        sys.e2eId = incomingId;
        sys.type = QStringLiteral("system");
        sys.text = encodeSystemNote(QT_TR_NOOP("Unknown message type %1. Update the client."),
            {message.value("rawType").toString()});
        const Placement placement = placeReceived(
            message.value("sentAt").toLongLong(), nowMillis(), orderFloor(peer, message));
        sys.ts = placement.displayTs;
        sys.orderKey = placement.orderKey;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        if (sys.id == 0) {
            return false;
        }
        emit rowAdded(peer, sys.id, message.value("sentByUs").toBool());
        return true;
    }

    if (message.value("text").toString().isEmpty()
        && message.value("attName").toString().isEmpty()
        && message.value("attRef").toString().isEmpty()
        && message.value("attSize").toLongLong() <= 0
        && message.value("keyboard").toString().isEmpty()) {
        bazarish::log::info("silent control message ({}) not shown", type.toStdString());
        return true;
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
    if (message.contains(QStringLiteral("group"))) {
        m.author = message.value("author").toString();
        m.contactable = message.value("contactable").toBool();
        m.fromHost = message.value("fromHost").toBool();
    }
    const Placement placement = placeReceived(
        message.value("sentAt").toLongLong(), nowMillis(), orderFloor(peer, message));
    m.ts = placement.displayTs;
    m.orderKey = placement.orderKey;
    m.status = m.outgoing
        ? (peer == contacts_.ownFingerprint() ? DeliveryStatus::Delivered
                                               : DeliveryStatus::AtRecipientServer)
        : DeliveryStatus::Received;
    m.id = store_.append(m);
    if (m.id == 0) {
        return false;
    }
    if (m.outgoing && !m.e2eId.isEmpty() && receiptsAhead_.value(peer).contains(m.e2eId)) {
        receiptsAhead_[peer].remove(m.e2eId);
        if (!markOutgoingRead(peer, m.id)) {
            return false;
        }
    }

    if (m.type == QStringLiteral("image") && !m.e2eId.isEmpty()) {
        const bool held = !store_.media(QStringLiteral("picture:") + m.e2eId).isEmpty();
        if (!store_.setHasPicture(m.id, held)) {
            return false;
        }
    }
    emit rowAdded(peer, m.id, false);
    if (!m.outgoing) {
        emit messageArrived(peer);
    }
    return true;
}

bool MessageIngest::noteGroupBlock(const QString& peer, const QVariantMap& message)
{
    const std::optional<GroupState> held = store_.groupState(peer);
    if (!message.contains(QStringLiteral("group"))) {
        if (held && message.value("sentAt").toLongLong() > held->at) {
            if (!store_.dropGroupState(peer)) {
                return false;
            }
            emit groupChanged(peer);
        }
        return true;
    }
    const QVariantMap block = message.value("group").toMap();
    GroupState state;
    state.kind = block.value("kind").toString();
    state.members = block.value("members").toLongLong();
    state.write = block.value("write").toBool();
    state.admin = block.value("admin").toBool();
    state.at = block.value("at").toLongLong();
    if (held && held->at > state.at) {
        return true;
    }
    if (!store_.setGroupState(peer, state)) {
        return false;
    }
    emit groupChanged(peer);
    return true;
}

bool MessageIngest::applyGroupReactions(const QString& peer, const QString& json)
{
    const QJsonArray entries = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue& entry : entries) {
        const QJsonObject item = entry.toObject();
        QMap<QString, qint64> counts;
        const QJsonObject given = item.value(QStringLiteral("counts")).toObject();
        for (auto it = given.begin(); it != given.end(); ++it) {
            counts.insert(it.key(), it.value().toInteger());
        }
        if (!store_.setGroupReactionCounts(
                peer, item.value(QStringLiteral("ref")).toString(), counts)) {
            return false;
        }
    }
    emit reactionsChanged();
    return true;
}

bool MessageIngest::markOutgoingRead(const QString& peer, const qint64 uptoId)
{
    if (!store_.markOutgoingReadUpTo(peer, uptoId, DeliveryStatus::Delivered,
            DeliveryStatus::AtRecipientServer, DeliveryStatus::AtRecipientServer)) {
        return false;
    }
    emit deliveredThrough(peer, uptoId);
    return true;
}

}  // namespace bazarish::app
