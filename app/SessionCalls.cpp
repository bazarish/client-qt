// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"





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

// Calls: what the window shows while one is up, and what it leaves behind.

void SessionController::startCall(const QString& peer)
{
    const QString target = peer.isEmpty() ? activePeer_ : peer;
    if (target.isEmpty()) {
        return;
    }
    emit requestStartCall(target);
}

void SessionController::acceptCall()
{
    emit requestAcceptCall(callId_);
}

void SessionController::declineCall()
{
    callEndedLocally_ = true;
    callTones_.stop();
    // Remembered until another call arrives: the sync thread may already have a
    // state update for this one on its way, and applying it after the refusal
    // put the window back on the desktop and the ringtone with it.
    refusedCallId_ = callId_;
    // Refused is over, here and now: the ringtone stops, the window goes and the
    // buttons come back at once, while the refusal itself travels in the
    // background. Waiting for it meant ringing at somebody who had already been
    // refused, for as long as I2P took to carry the word.
    const QString callId = callId_;
    onCallStateChanged(0, QString(), QString(), false, QString(), false, 0, 0.0F, 0.0F);
    emit requestDeclineCall(callId);
}

void SessionController::endCall()
{
    // Hanging up is not a call that failed: it ends the tones here rather than
    // letting the outcome speak for it.
    callEndedLocally_ = true;
    callTones_.stop();
    emit requestEndCall();
}

void SessionController::setCallMuted(const bool muted)
{
    emit requestSetCallMuted(muted);
}

void SessionController::onCallStateChanged(const int state, const QString& peer,
    const QString& callId, const bool muted, const QString& stage, const bool peerRinging,
    const qint64 connectedAtMs, const float inputLevel, const float outputLevel)
{
    // The levels move on every tick and nothing else does: they have their own
    // signal, so a level meter does not re-evaluate the whole call window.
    if (!qFuzzyCompare(static_cast<qreal>(callInputLevel_), static_cast<qreal>(inputLevel))
        || !qFuzzyCompare(static_cast<qreal>(callOutputLevel_), static_cast<qreal>(outputLevel))) {
        callInputLevel_ = inputLevel;
        callOutputLevel_ = outputLevel;
        emit callLevelsChanged();
    }
    static const char* const kNames[] = {"idle", "outgoing", "incoming", "active"};
    const QString name = (state >= 0 && state <= 3) ? QString::fromLatin1(kNames[state])
                                                    : QStringLiteral("idle");
    // A call the user refused is over here, whatever is still in flight about it.
    if (!refusedCallId_.isEmpty() && callId == refusedCallId_
        && name != QLatin1String("idle")) {
        return;
    }
    if (!callId.isEmpty() && callId != refusedCallId_) {
        refusedCallId_.clear();
    }
    if (callState_ == name && callPeer_ == peer && callId_ == callId && callMuted_ == muted
        && callStage_ == stage && callConnectedAtMs_ == connectedAtMs) {
        return;
    }
    callStage_ = stage;
    callConnectedAtMs_ = connectedAtMs;
    callState_ = name;
    callPeer_ = peer;
    callId_ = callId;
    callMuted_ = muted;
    emit callChanged();

    // Call-progress tones: silence while the invitation is still travelling, a
    // ringback from the moment a device of theirs is showing the call until there
    // is a voice to hear.
    const bool waitingOnThem = (name == QLatin1String("outgoing") && peerRinging)
        || (name == QLatin1String("active") && connectedAtMs == 0);
    if (waitingOnThem) {
        callTones_.ringback();
    } else if (name == QLatin1String("idle")) {
        callTones_.endRingback();
        callEndedLocally_ = false;
    } else {
        callTones_.stop();
    }

    // Surface the call as a background operation: it begins on an outgoing/incoming
    // invite and the active leg, and finishes when the call returns to idle.
    if (state == 0) {  // idle: the call (if any) ended
        if (!callOpId_.isEmpty()) {
            finishOperation(callOpId_, true, QStringLiteral("Call ended"));
            callOpId_.clear();
        }
    } else {
        const QString opId = QStringLiteral("call:") + (callId.isEmpty() ? peer : callId);
        const QString title = QStringLiteral("Call with ") + peerName(peer);
        const QString status = state == 1 ? QStringLiteral("Calling…")
            : state == 2                  ? QStringLiteral("Incoming call…")
                                          : QStringLiteral("Connected");
        if (callOpId_ != opId) {
            callOpId_ = opId;
            beginOperation(opId, QStringLiteral("call"), title, status, peer);
        } else {
            updateOperation(opId, status);
        }
    }
}

void SessionController::onCallLogged(
    const QString& peer, const bool incoming, const int outcome, qint64 durationSec)
{
    (void)durationSec;  // nothing is written down, so its length is nobody's business
    if (peer.isEmpty()) {
        return;
    }
    // Outcome ints mirror Session::CallOutcome: 0 answered, 1 no-answer, 2 declined,
    // 3 missed, 4 cancelled, 5 busy, 6 refused (the peer takes no calls), 7 refused
    // here (this account takes none, or none from them).
    // A call of ours that did not happen says so out loud: the user is not
    // necessarily looking at the window when the far end refuses.
    if (!incoming && !callEndedLocally_
        && (outcome == static_cast<int>(Session::CallOutcome::eNoAnswer)
            || outcome == static_cast<int>(Session::CallOutcome::eDeclined)
            || outcome == static_cast<int>(Session::CallOutcome::eBusy)
            || outcome == static_cast<int>(Session::CallOutcome::eRefused))) {
        callTones_.failure();
    }
    // A call leaves nothing behind in the conversation: no line in it and no
    // preview in the chat list. A call is a thing that happened at the time it
    // happened - it is not correspondence, and a column of "Missed call" over a
    // chat says nothing the user did not already see the window say.
}

}  // namespace bazarish::app
