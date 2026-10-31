// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "AudioRoute.hpp"
#include "SystemNotes.hpp"

#include <QAudioDevice>

namespace bazarish::app {

bool SessionController::speakerPresent()
{
    if (AudioRoute::instance().output().isNull()) {
        emit actionFailed(tr("No audio output device."));
        return false;
    }
    return true;
}

void SessionController::startCall(const QString& peer)
{
    const QString target = peer.isEmpty() ? activePeer_ : peer;
    if (target.isEmpty() || !speakerPresent()) {
        return;
    }
    emit commands_.requestStartCall(target);
}

void SessionController::acceptCall()
{
    if (!speakerPresent()) {
        return;
    }
    emit commands_.requestAcceptCall(callId_);
}

QString SessionController::callStage() const
{
    return systemNoteText(callStage_);
}

void SessionController::declineCall()
{
    emit commands_.declineCall();
}

void SessionController::endCall()
{
    emit commands_.endCall();
}

void SessionController::setCallMuted(const bool muted)
{
    emit commands_.requestSetCallMuted(muted);
}

void SessionController::onCallStateChanged(const int state, const QString& peer,
    const QString& callId, const bool muted, const QString& stage, const bool peerRinging,
    const qint64 connectedAtMs, const float inputLevel, const float outputLevel)
{
    Q_UNUSED(peerRinging);
    if (!qFuzzyCompare(static_cast<qreal>(callInputLevel_), static_cast<qreal>(inputLevel))
        || !qFuzzyCompare(static_cast<qreal>(callOutputLevel_), static_cast<qreal>(outputLevel))) {
        callInputLevel_ = inputLevel;
        callOutputLevel_ = outputLevel;
        emit callLevelsChanged();
    }
    static const char* const kNames[] = {"idle", "outgoing", "incoming", "active"};
    const QString name = (state >= 0 && state <= 3) ? QString::fromLatin1(kNames[state])
                                                    : QStringLiteral("idle");
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
}

}  // namespace bazarish::app
