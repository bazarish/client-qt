// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"

#include <utility>

namespace bazarish::app {

QStringList SessionController::standardReactions() const
{
    return bazarish::app::standardReactions();
}

void SessionController::forgetReactionFlash()
{
    if (!activePeer_.isEmpty()) {
        emit commands_.forgetReactionFlash(activePeer_);
    }
}

void SessionController::react(const QString& e2eId, const QString& emoji)
{
    if (!activePeer_.isEmpty() && !e2eId.isEmpty()) {
        emit commands_.react(activePeer_, e2eId, emoji);
    }
}

void SessionController::rememberEmoji(const QString& emoji)
{
    emit commands_.rememberEmoji(emoji);
}

RowReactions SessionController::reactionsOf(const QString& e2eId) const
{
    RowReactions out;
    if (activePeer_.isEmpty() || e2eId.isEmpty()) {
        return out;
    }
    QStringList order;
    QHash<QString, int> counts;
    QString mine;
    if (activeGroup_) {
        const QMap<QString, qint64> theirs = store_.groupReactionCounts(activePeer_, e2eId);
        for (auto it = theirs.cbegin(); it != theirs.cend(); ++it) {
            order << it.key();
            counts[it.key()] = static_cast<int>(it.value());
        }
    }
    const bool flashing = reactionsToFlash_.contains(reactionFlashKey(activePeer_, e2eId));
    for (const Reaction& r : store_.reactionsFor(activePeer_, e2eId)) {
        if (flashing && r.reactor != fingerprint_ && !r.emoji.isEmpty()) {
            out.fresh << r.emoji;
        }
        if (activeGroup_ && r.reactor != fingerprint_) {
            continue;
        }
        if (!counts.contains(r.emoji)) {
            order << r.emoji;
        }
        ++counts[r.emoji];
        if (r.reactor == fingerprint_) {
            mine = r.emoji;
        }
    }
    for (const QString& e : order) {
        out.summary << QVariantMap{{QStringLiteral("emoji"), e},
            {QStringLiteral("count"), counts.value(e)}, {QStringLiteral("mine"), e == mine}};
    }
    return out;
}

void SessionController::putReactionsFor(const QVector<StoredMessage>& messages)
{
    for (const StoredMessage& message : messages) {
        if (!message.e2eId.isEmpty()) {
            conversation_.putReactions(message.e2eId, reactionsOf(message.e2eId));
        }
    }
}

void SessionController::onReactionsChanged(const QString& peer, const QString& e2eId)
{
    if (peer != activePeer_) {
        return;
    }
    const QStringList targets = e2eId.isEmpty() ? conversation_.loadedE2eIds() : QStringList{e2eId};
    for (const QString& target : targets) {
        if (conversation_.rowForE2e(target) >= 0) {
            conversation_.putReactions(target, reactionsOf(target));
        }
    }
}

void SessionController::onReactionsToFlash(const QStringList& flashing)
{
    if (reactionsToFlash_ == flashing) {
        return;
    }
    const QStringList before = std::exchange(reactionsToFlash_, flashing);
    bool shownFlashing = false;
    for (const QString& target : conversation_.loadedE2eIds()) {
        const QString key = reactionFlashKey(activePeer_, target);
        const bool now = flashing.contains(key);
        shownFlashing = shownFlashing || now;
        if (now != before.contains(key)) {
            conversation_.putReactions(target, reactionsOf(target));
        }
    }
    if (shownFlashing) {
        emit reactionsFlashing();
    }
}

}  // namespace bazarish::app
