// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"

#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

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
    refusedCallId_ = callId_;
    const QString callId = callId_;
    onCallStateChanged(0, QString(), QString(), false, QString(), false, 0, 0.0F, 0.0F);
    emit requestDeclineCall(callId);
}

void SessionController::endCall()
{
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
    if (!qFuzzyCompare(static_cast<qreal>(callInputLevel_), static_cast<qreal>(inputLevel))
        || !qFuzzyCompare(static_cast<qreal>(callOutputLevel_), static_cast<qreal>(outputLevel))) {
        callInputLevel_ = inputLevel;
        callOutputLevel_ = outputLevel;
        emit callLevelsChanged();
    }
    static const char* const kNames[] = {"idle", "outgoing", "incoming", "active"};
    const QString name = (state >= 0 && state <= 3) ? QString::fromLatin1(kNames[state])
                                                    : QStringLiteral("idle");
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

    if (state == 0) {
        if (!callOpId_.isEmpty()) {
            finishOperation(callOpId_, true, tr("Call ended"));
            callOpId_.clear();
        }
    } else {
        const QString opId = QStringLiteral("call:") + (callId.isEmpty() ? peer : callId);
        const QString title = tr("Call with %1").arg(peerName(peer));
        const QString status = state == 1 ? tr("Calling…")
            : state == 2                  ? tr("Incoming call…")
                                          : tr("Connected");
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
    (void)durationSec;
    if (peer.isEmpty()) {
        return;
    }
    if (!incoming && !callEndedLocally_
        && (outcome == static_cast<int>(Session::CallOutcome::eNoAnswer)
            || outcome == static_cast<int>(Session::CallOutcome::eDeclined)
            || outcome == static_cast<int>(Session::CallOutcome::eBusy)
            || outcome == static_cast<int>(Session::CallOutcome::eRefused))) {
        callTones_.failure();
    }
}

}  // namespace bazarish::app
