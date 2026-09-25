// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"


#include <QJsonDocument>

#include <QJsonArray>


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
// How many reactions outside the standard set the picker remembers.
constexpr int kRecentReactions = 5;
// How many unseen reactions are remembered for their flash. A person who has
// been away comes back to a handful of them, not to a list that grew all week;
// past this the oldest is dropped, and the reaction is still there to be read -
// only its flash is not.
constexpr int kReactionsToFlash = 64;
// What one pending flash is written as: the conversation and the message, which
// together name the reaction wherever the chat is scrolled to.
QString flashKey(const QString& peer, const QString& target)
{
    return peer + "\n" + target;
}
// The reactions offered without being asked for. Anything else a user reaches
// for - typed, or tapped on someone else's chip - is theirs, and is remembered.
const QStringList kStandardReactions = {QStringLiteral("\U0001F44D"),
    QStringLiteral("\u2764\uFE0F"), QStringLiteral("\U0001F602"),
    QStringLiteral("\U0001F389"), QStringLiteral("\U0001F525"), QStringLiteral("\U0001F62E"),
    QStringLiteral("\U0001F622"), QStringLiteral("\U0001F64F"), QStringLiteral("\U0001F440"),
    QStringLiteral("\u2705"), QStringLiteral("\U0001F4AF"), QStringLiteral("\U0001F680"),
    QStringLiteral("\U0001F621"), QStringLiteral("\U0001F44F"), QStringLiteral("\U0001F914"),
    QStringLiteral("\U0001F44E"), QStringLiteral("\U0001F91D"), QStringLiteral("\U0001F529")};
}  // namespace

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

// Reactions: the set offered, the ones recently used, and the flash on open.

QStringList SessionController::standardReactions() const
{
    return kStandardReactions;
}

void SessionController::rememberReaction(const QString& emoji)
{
    const QString trimmed = emoji.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    recentReactions_.removeAll(trimmed);
    recentReactions_.prepend(trimmed);
    while (recentReactions_.size() > kRecentReactions) {
        recentReactions_.removeLast();
    }
    QJsonArray array;
    for (const QString& entry : recentReactions_) {
        array.append(entry);
    }
    const QByteArray text = QJsonDocument(array).toJson(QJsonDocument::Compact);
    accountDb().putText("recent-reactions", text.toStdString());
    emit recentReactionsChanged();
}

void SessionController::noteReactionToFlash(const QString& peer, const QString& target)
{
    const QString key = flashKey(peer, target);
    reactionsToFlash_.removeAll(key);
    reactionsToFlash_.append(key);
    while (reactionsToFlash_.size() > kReactionsToFlash) {
        reactionsToFlash_.removeFirst();
    }
    persistReactionsToFlash();
}

void SessionController::persistReactionsToFlash()
{
    QJsonArray array;
    for (const QString& entry : reactionsToFlash_) {
        array.append(entry);
    }
    accountDb().putText("reactions-to-flash",
        QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString());
}

QStringList SessionController::reactionsToFlash(const QString& e2eId) const
{
    if (activePeer_.isEmpty() || e2eId.isEmpty()
        || !reactionsToFlash_.contains(flashKey(activePeer_, e2eId))) {
        return {};
    }
    // Which of the emoji on this message to flash: the ones somebody else put
    // there. Our own, echoed from another device of ours, was never news.
    QStringList emoji;
    for (const Reaction& reaction : store_.reactionsFor(activePeer_, e2eId)) {
        if (reaction.reactor != fingerprint_ && !reaction.emoji.isEmpty()) {
            emoji << reaction.emoji;
        }
    }
    return emoji;
}

void SessionController::forgetReactionFlash()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString prefix = activePeer_ + "\n";
    const qsizetype before = reactionsToFlash_.size();
    reactionsToFlash_.removeIf(
        [&prefix](const QString& entry) { return entry.startsWith(prefix); });
    if (reactionsToFlash_.size() == before) {
        return;
    }
    persistReactionsToFlash();
    // The chips are what read this, and they re-read on this revision.
    ++reactionsRevision_;
    emit reactionsRevisionChanged();
}

void SessionController::react(const QString& e2eId, const QString& emoji)
{
    if (activePeer_.isEmpty() || e2eId.isEmpty()) {
        return;
    }
    // Toggle: tapping the emoji we already set removes our reaction.
    const QString next = (myReaction(e2eId) == emoji) ? QString() : emoji;
    // Setting one they reached for outside the standard set - typed, or tapped on
    // somebody else's chip - puts it in their recents. Removing one does not.
    if (!next.isEmpty() && !kStandardReactions.contains(next)) {
        rememberReaction(next);
    }
    store_.setReaction(activePeer_, e2eId, fingerprint_, next);
    emit requestSendReaction(activePeer_, e2eId, next);
    ++reactionsRevision_;
    emit reactionsRevisionChanged();
}

QString SessionController::myReaction(const QString& e2eId) const
{
    for (const Reaction& r : store_.reactionsFor(activePeer_, e2eId)) {
        if (r.reactor == fingerprint_) {
            return r.emoji;
        }
    }
    return {};
}

QVariantList SessionController::reactionSummary(const QString& e2eId) const
{
    QVariantList out;
    if (activePeer_.isEmpty() || e2eId.isEmpty()) {
        return out;
    }
    // Aggregate by emoji, preserving the order each emoji was first seen.
    QStringList order;
    QHash<QString, int> counts;
    QString mine;
    for (const Reaction& r : store_.reactionsFor(activePeer_, e2eId)) {
        if (!counts.contains(r.emoji)) {
            order << r.emoji;
        }
        ++counts[r.emoji];
        if (r.reactor == fingerprint_) {
            mine = r.emoji;
        }
    }
    for (const QString& e : order) {
        QVariantMap m;
        m[QStringLiteral("emoji")] = e;
        m[QStringLiteral("count")] = counts.value(e);
        m[QStringLiteral("mine")] = (e == mine);
        out << m;
    }
    return out;
}

}  // namespace bazarish::app
