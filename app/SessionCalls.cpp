// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "AudioRoute.hpp"
#include "SystemNotes.hpp"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QLatin1StringView>
#include <QPermissions>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace bazarish::app {

namespace {

constexpr std::size_t kCallStates = 4;

}  // namespace

bool SessionController::speakerPresent()
{
    if (AudioRoute::instance().output().isNull()) {
        emit actionFailed(tr("No audio output device"));
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
    const QMicrophonePermission microphone;
    switch (qApp->checkPermission(microphone)) {
    case Qt::PermissionStatus::Granted:
        emit commands_.acceptCall();
        return;
    case Qt::PermissionStatus::Denied:
        emit microphoneRefused();
        return;
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(microphone, this, [this](const QPermission& asked) {
            if (asked.status() == Qt::PermissionStatus::Granted) {
                emit commands_.acceptCall();
            } else {
                emit microphoneRefused();
            }
        });
        return;
    }
    throw std::logic_error("unknown microphone permission status");
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

void SessionController::onCallLevels(const float inputLevel, const float outputLevel)
{
    callInputLevel_ = inputLevel;
    callOutputLevel_ = outputLevel;
    emit callLevelsChanged();
}

void SessionController::onCallStateChanged(const int state, const QString& peer,
    const QString&, const bool muted, const QString& stage, const qint64 connectedAtMs)
{
    static constexpr std::array<QLatin1StringView, kCallStates> kNames
        = {QLatin1StringView("idle"), QLatin1StringView("outgoing"),
            QLatin1StringView("incoming"), QLatin1StringView("active")};
    if (state < 0 || static_cast<std::size_t>(state) >= kNames.size()) {
        throw std::logic_error("no call state " + std::to_string(state));
    }
    const QString name(kNames[static_cast<std::size_t>(state)]);
    if (callState_ == name && callPeer_ == peer && callMuted_ == muted
        && callStage_ == stage && callConnectedAtMs_ == connectedAtMs) {
        return;
    }
    callStage_ = stage;
    callConnectedAtMs_ = connectedAtMs;
    callState_ = name;
    callPeer_ = peer;
    callMuted_ = muted;
    emit callChanged();
}

}  // namespace bazarish::app
