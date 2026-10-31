// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "AccountDb.hpp"
#include "DeliveryStatus.hpp"
#include "SessionShared.hpp"
#include "SystemNotes.hpp"

#include "Session.hpp"

#include <QJsonArray>
#include <QJsonDocument>

namespace bazarish::app {

namespace {
constexpr int kRecentReactions = 6;
constexpr int kRecentEmoji = 40;

QStringList withNewest(QStringList list, const QString& item, const int limit)
{
    list.removeAll(item);
    list.prepend(item);
    while (list.size() > limit) {
        list.removeLast();
    }
    return list;
}

std::string asJson(const QStringList& list)
{
    return QJsonDocument(QJsonArray::fromStringList(list))
        .toJson(QJsonDocument::Compact)
        .toStdString();
}
}  // namespace

void CoreAccount::react(const QString& peer, const QString& e2eId, const QString& emoji)
{
    QString mine;
    for (const Reaction& reaction : store_.reactionsFor(peer, e2eId)) {
        if (reaction.reactor == contacts_.ownFingerprint()) {
            mine = reaction.emoji;
        }
    }
    const QString next = mine == emoji ? QString() : emoji;
    if (!next.isEmpty() && !standardReactions().contains(next)) {
        rememberReaction(next);
    }
    if (!store_.setReaction(peer, e2eId, contacts_.ownFingerprint(), next)) {
        reportUnsaved();
        return;
    }
    emit commands_.requestSendReaction(peer, e2eId, next);
    emit events_.reactionsChanged();
}

void CoreAccount::rememberReaction(const QString& emoji)
{
    const QString trimmed = emoji.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    recentReactions_ = withNewest(recentReactions_, trimmed, kRecentReactions);
    accountDb().putText("recent-reactions", asJson(recentReactions_));
    emit events_.recentReactions(recentReactions_);
}

void CoreAccount::rememberEmoji(const QString& emoji)
{
    if (emoji.isEmpty()) {
        return;
    }
    recentEmoji_ = withNewest(recentEmoji_, emoji, kRecentEmoji);
    accountDb().putText("recent-emoji", asJson(recentEmoji_));
    emit events_.recentEmoji(recentEmoji_);
}

void CoreAccount::pinChat(const QString& peer, const bool pinned)
{
    if (!store_.setPinned(peer, pinned)) {
        reportUnsaved();
        return;
    }
    emit events_.chatsChanged();
    emit commands_.requestSyncChatPin(peer, pinned);
}

void CoreAccount::clearChat(const QString& peer, const bool forEveryone)
{
    if (!store_.clearPeer(peer)) {
        reportUnsaved();
        return;
    }
    if (!forEveryone) {
        emit commands_.requestSyncChatClear(peer);
        emit events_.peerReset(peer);
        return;
    }
    emit commands_.requestClearChatForEveryone(peer);
    StoredMessage sys;
    sys.peer = peer;
    sys.type = QStringLiteral("system");
    sys.text = encodeSystemNote(QT_TR_NOOP("You cleared the chat for everyone."));
    sys.ts = nowMillis();
    sys.orderKey = sys.ts;
    sys.status = DeliveryStatus::Received;
    if (store_.append(sys) == 0) {
        reportUnsaved();
    }
    emit events_.peerReset(peer);
}

void CoreAccount::setChatTimer(const QString& peer, const qint64 seconds)
{
    if (peer.isEmpty() || hasAliasSigil(peer) || store_.groupState(peer).has_value()
        || !client::isChatTimerChoice(seconds) || store_.chatTimer(peer) == seconds) {
        return;
    }
    if (!store_.setChatTimer(peer, seconds)) {
        reportUnsaved();
        return;
    }
    emit commands_.requestSendChatTimer(peer, seconds);
    emit events_.chatTimerChanged(peer, seconds);
    StoredMessage sys;
    sys.peer = peer;
    sys.type = QStringLiteral("system");
    sys.text = chatTimerNote(seconds, /*ours=*/true, contacts_.peerName(peer));
    sys.ts = nowMillis();
    sys.orderKey = sys.ts;
    sys.status = DeliveryStatus::Received;
    sys.id = store_.append(sys);
    if (sys.id == 0) {
        reportUnsaved();
        return;
    }
    emit events_.rowAdded(peer, sys.id, true);
}

void CoreAccount::sweepExpired()
{
    for (const auto& [peer, id] : store_.expiredBy(nowMillis())) {
        if (!store_.removeById(id)) {
            reportUnsaved();
            return;
        }
        statusById_.remove(id);
        emit events_.rowRemoved(peer, id);
        emit events_.chatChanged(peer);
    }
}

void CoreAccount::forgetChat(const QString& peer)
{
    if (!store_.forgetPeer(peer)) {
        reportUnsaved();
        return;
    }
    emit events_.peerForgotten(peer);
}

void CoreAccount::clearSavedEverywhere()
{
    const QString saved = contacts_.ownFingerprint();
    if (!store_.clearPeer(saved)) {
        reportUnsaved();
        return;
    }
    emit events_.peerReset(saved);
    emit events_.chatsChanged();
    emit commands_.requestClearSaved();
}

}  // namespace bazarish::app
