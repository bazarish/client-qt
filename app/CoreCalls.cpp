// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "Session.hpp"
#include "SystemNotes.hpp"

#include <array>

namespace bazarish::app {

namespace {

const QString kIdle = QStringLiteral("idle");

QString callStateName(const int state)
{
    static const std::array<const char*, 4> kNames = {"idle", "outgoing", "incoming", "active"};
    if (state < 0 || state >= static_cast<int>(kNames.size())) {
        return kIdle;
    }
    return QString::fromLatin1(kNames.at(static_cast<std::size_t>(state)));
}

}  // namespace

void CoreAccount::onCallStateChanged(const int state, const QString& peer, const QString& callId,
    const bool muted, const QString& stage, const bool peerRinging, const qint64 connectedAtMs,
    const float inputLevel, const float outputLevel)
{
    const QString name = callStateName(state);
    if (!refusedCallId_.isEmpty() && callId == refusedCallId_ && name != kIdle) {
        return;
    }
    if (!callId.isEmpty() && callId != refusedCallId_) {
        refusedCallId_.clear();
    }
    emit events_.callStateChanged(state, peer, callId, muted, stage, peerRinging, connectedAtMs,
        inputLevel, outputLevel);
    if (call_.state == name && call_.peer == peer && call_.id == callId && call_.stage == stage
        && call_.connectedAtMs == connectedAtMs) {
        return;
    }
    call_.state = name;
    call_.peer = peer;
    call_.id = callId;
    call_.stage = stage;
    call_.connectedAtMs = connectedAtMs;
    emit callChanged();

    const bool waitingOnThem = (name == QLatin1String("outgoing") && peerRinging)
        || (name == QLatin1String("active") && connectedAtMs == 0);
    if (waitingOnThem) {
        callTones_.ringback();
    } else if (name == kIdle) {
        callTones_.endRingback();
        callEndedLocally_ = false;
    } else {
        callTones_.stop();
    }

    if (name == kIdle) {
        if (!callOpId_.isEmpty()) {
            log_.finish(callOpId_, true, tr("Call ended"));
            callOpId_.clear();
        }
        return;
    }
    const QString opId = QStringLiteral("call:") + (callId.isEmpty() ? peer : callId);
    const QString status = name == QLatin1String("outgoing") ? tr("Calling…")
        : name == QLatin1String("incoming")                  ? tr("Incoming call…")
                                                             : tr("Connected");
    if (callOpId_ != opId) {
        callOpId_ = opId;
        log_.begin(opId, QStringLiteral("call"), tr("Call with %1").arg(contacts_.peerName(peer)),
            status, peer);
    } else {
        log_.update(opId, status);
    }
}

void CoreAccount::onCallLogged(
    const QString& peer, const bool incoming, const int outcome, const qint64 durationSec)
{
    Q_UNUSED(durationSec);
    if (peer.isEmpty()) {
        return;
    }
    using Outcome = bazarish::client::Session::CallOutcome;
    if (!incoming && !callEndedLocally_
        && (outcome == static_cast<int>(Outcome::eNoAnswer)
            || outcome == static_cast<int>(Outcome::eDeclined)
            || outcome == static_cast<int>(Outcome::eBusy)
            || outcome == static_cast<int>(Outcome::eRefused))) {
        callTones_.failure();
    }
}

void CoreAccount::declineCall()
{
    callEndedLocally_ = true;
    callTones_.stop();
    refusedCallId_ = call_.id;
    const QString callId = call_.id;
    onCallStateChanged(0, QString(), QString(), false, QString(), false, 0, 0.0F, 0.0F);
    emit commands_.requestDeclineCall(callId);
}

void CoreAccount::endCall()
{
    callEndedLocally_ = true;
    callTones_.stop();
    emit commands_.requestEndCall();
}

}  // namespace bazarish::app
