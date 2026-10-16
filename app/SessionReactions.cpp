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


using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

QStringList SessionController::standardReactions() const
{
    return bazarish::app::standardReactions();
}

QStringList SessionController::reactionsToFlash(const QString& e2eId) const
{
    if (activePeer_.isEmpty() || e2eId.isEmpty()
        || !reactionsToFlash_.contains(reactionFlashKey(activePeer_, e2eId))) {
        return {};
    }
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

QVariantList SessionController::reactionSummary(const QString& e2eId) const
{
    QVariantList out;
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
    for (const Reaction& r : store_.reactionsFor(activePeer_, e2eId)) {
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
        QVariantMap m;
        m[QStringLiteral("emoji")] = e;
        m[QStringLiteral("count")] = counts.value(e);
        m[QStringLiteral("mine")] = (e == mine);
        out << m;
    }
    return out;
}

}  // namespace bazarish::app
