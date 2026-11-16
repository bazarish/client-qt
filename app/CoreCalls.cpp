// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "Session.hpp"

#include <stdexcept>
#include <string>

namespace bazarish::app {

namespace {

using CallState = client::Session::CallState;

CallState callStateOf(const int state)
{
    if (state < static_cast<int>(CallState::eIdle)
        || state > static_cast<int>(CallState::eActive)) {
        throw std::logic_error("no call state " + std::to_string(state));
    }
    return static_cast<CallState>(state);
}

}  // namespace

void CoreAccount::onCallStateChanged(const int state, const QString& peer, const QString& callId,
    const bool muted, const QString& stage, const bool peerRinging, const qint64 connectedAtMs)
{
    const CallState name = callStateOf(state);
    if (callEnding_ && (name == CallState::eIdle || callId != call_.id)) {
        callEnding_ = false;
    }
    emit events_.callStateChanged(state, peer, callId, muted, stage, connectedAtMs);
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

void CoreAccount::onCallLogged(const QString& peer, const bool incoming, const int outcome)
{
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

void CoreAccount::acceptCall()
{
    emit commands_.requestAcceptCall(call_.id);
}

void CoreAccount::declineCall()
{
    if (callEnding_) {
        return;
    }
    callEndedLocally_ = true;
    callEnding_ = true;
    callTones_.stop();
    emit callChanged();
    emit commands_.requestDeclineCall(call_.id);
}

void CoreAccount::endCall()
{
    if (callEnding_) {
        return;
    }
    callEndedLocally_ = true;
    callEnding_ = true;
    callTones_.stop();
    emit callChanged();
    emit commands_.requestEndCall();
}

void CoreAccount::onCallEndFailed(const bool declining, const QString& reason)
{
    callEndedLocally_ = false;
    callEnding_ = false;
    emit callChanged();
    emit events_.callEndFailed();
    emit events_.actionFailed(declining ? tr("The call was not declined: %1").arg(reason)
                                        : tr("The call was not ended: %1").arg(reason));
}

}  // namespace bazarish::app
