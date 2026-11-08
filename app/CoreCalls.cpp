// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "Session.hpp"

namespace bazarish::app {

namespace {

using CallState = client::Session::CallState;

CallState callStateOf(const int state)
{
    if (state < static_cast<int>(CallState::eIdle)
        || state > static_cast<int>(CallState::eActive)) {
        return CallState::eIdle;
    }
    return static_cast<CallState>(state);
}

}  // namespace

void CoreAccount::onCallStateChanged(const int state, const QString& peer, const QString& callId,
    const bool muted, const QString& stage, const bool peerRinging, const qint64 connectedAtMs,
    const float inputLevel, const float outputLevel)
{
    const CallState name = callStateOf(state);
    if (!refusedCallId_.isEmpty() && callId == refusedCallId_ && name != CallState::eIdle) {
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
    const bool wasAnswered = call_.state == CallState::eActive;
    call_.state = name;
    call_.peer = peer;
    call_.id = callId;
    call_.stage = stage;
    call_.connectedAtMs = connectedAtMs;
    emit callChanged();

    const bool waitingOnThem = (name == CallState::eOutgoing && peerRinging)
        || (name == CallState::eActive && connectedAtMs == 0);
    if (waitingOnThem) {
        callTones_.ringback();
    } else if (name == CallState::eIdle) {
        if (wasAnswered) {
            callTones_.hangup();
        } else {
            callTones_.endRingback();
        }
        callEndedLocally_ = false;
    } else {
        callTones_.stop();
    }

    if (name == CallState::eIdle) {
        if (!callOpId_.isEmpty()) {
            log_.finish(callOpId_, true, tr("Call ended"));
            callOpId_.clear();
        }
        return;
    }
    const QString opId = QStringLiteral("call:") + (callId.isEmpty() ? peer : callId);
    const QString status = name == CallState::eOutgoing ? tr("Calling…")
        : name == CallState::eIncoming                  ? tr("Incoming call…")
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
