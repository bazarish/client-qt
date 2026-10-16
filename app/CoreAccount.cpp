// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "SessionWorker.hpp"

#include <QMetaMethod>

#include <exception>
#include <utility>

namespace bazarish::app {

namespace {

const int kLoginSignerMetaType
    = qRegisterMetaType<std::shared_ptr<bazarish::client::LoginSigner>>(
        "std::shared_ptr<bazarish::client::LoginSigner>");
const int kContactStateMetaType
    = qRegisterMetaType<QVector<ContactState>>("QVector<ContactState>");


}  // namespace

CoreAccount::CoreAccount(AccountCommands& commands, AccountEvents& events, QObject* const parent)
    : QObject(parent)
    , commands_(commands)
    , events_(events)
{
    worker_ = new SessionWorker();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(&commands_, &AccountCommands::requestOpen, worker_, &SessionWorker::openAccount);
    connect(&commands_, &AccountCommands::requestConnect, worker_,
        &SessionWorker::connectAndRegister);
    connect(&commands_, &AccountCommands::requestSendText, worker_, &SessionWorker::sendText);
    connect(&commands_, &AccountCommands::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(&commands_, &AccountCommands::requestSendPicture, worker_, &SessionWorker::sendPicture);
    connect(&commands_, &AccountCommands::requestSendVoice, worker_, &SessionWorker::sendVoice);
    connect(&commands_, &AccountCommands::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(&commands_, &AccountCommands::requestSendReaction, worker_,
        &SessionWorker::sendReaction);
    connect(&commands_, &AccountCommands::requestSendCallback, worker_,
        &SessionWorker::sendCallback);
    connect(&commands_, &AccountCommands::requestSendCommand, worker_, &SessionWorker::sendCommand);
    connect(&commands_, &AccountCommands::requestSendEdit, worker_, &SessionWorker::sendEdit);
    connect(&commands_, &AccountCommands::requestSendDelete, worker_, &SessionWorker::sendDelete);
    connect(&commands_, &AccountCommands::requestUnsend, worker_, &SessionWorker::dropSentFile);
    connect(&commands_, &AccountCommands::requestSetAvatar, worker_, &SessionWorker::setAvatar);
    connect(&commands_, &AccountCommands::requestClearAvatar, worker_, &SessionWorker::clearAvatar);
    connect(&commands_, &AccountCommands::requestSetDisplayName, worker_,
        &SessionWorker::setDisplayName);
    connect(&commands_, &AccountCommands::requestRenameContact, worker_,
        &SessionWorker::renameContact);
    connect(&commands_, &AccountCommands::requestRemoveContact, worker_,
        &SessionWorker::removeContact);
    connect(&commands_, &AccountCommands::requestClearSaved, worker_, &SessionWorker::clearSaved);
    connect(&commands_, &AccountCommands::requestSetBlocked, worker_, &SessionWorker::setBlocked);
    connect(&commands_, &AccountCommands::requestSetContactNotifications, worker_,
        &SessionWorker::setContactNotifications);
    connect(&commands_, &AccountCommands::requestSetContactCalls, worker_,
        &SessionWorker::setContactCalls);
    connect(&commands_, &AccountCommands::requestSyncChatPin, worker_, &SessionWorker::syncChatPin);
    connect(&commands_, &AccountCommands::requestSyncRead, worker_, &SessionWorker::syncRead);
    connect(&commands_, &AccountCommands::requestSyncChatClear, worker_,
        &SessionWorker::syncChatClear);
    connect(&commands_, &AccountCommands::requestEmitSettings, worker_,
        &SessionWorker::emitSettings);
    connect(&commands_, &AccountCommands::requestClearChatForEveryone, worker_,
        &SessionWorker::clearChatForEveryone);
    connect(&commands_, &AccountCommands::requestAddByInvite, worker_, &SessionWorker::addByInvite);
    connect(&commands_, &AccountCommands::requestAddByAlias, worker_, &SessionWorker::addByAlias);
    connect(&commands_, &AccountCommands::requestAcceptContact, worker_,
        &SessionWorker::acceptContact);
    connect(&commands_, &AccountCommands::requestInviteSig, worker_, &SessionWorker::requestInvite);
    connect(&commands_, &AccountCommands::requestPublishThisDeviceAddress, worker_,
        &SessionWorker::publishThisDeviceAddress);
    connect(&commands_, &AccountCommands::requestPublishFreshAddress, worker_,
        &SessionWorker::publishFreshAddress);
    connect(&commands_, &AccountCommands::requestConnectionLog, worker_,
        &SessionWorker::connectionLog);
    connect(&commands_, &AccountCommands::requestContactsFromDevices, worker_,
        &SessionWorker::askDevicesForContacts);
    connect(&commands_, &AccountCommands::requestClearConnectionLog, worker_,
        &SessionWorker::clearConnectionLog);
    connect(&commands_, &AccountCommands::requestSaveAttachment, worker_,
        &SessionWorker::saveAttachment);
    connect(&commands_, &AccountCommands::requestExport, worker_, &SessionWorker::exportAccount);
    connect(&commands_, &AccountCommands::requestStartPairing, worker_,
        &SessionWorker::startPairing);
    connect(&commands_, &AccountCommands::requestStopPairing, worker_, &SessionWorker::stopPairing);
    connect(&commands_, &AccountCommands::requestChangePassphrase, worker_,
        &SessionWorker::changePassphrase);
    connect(&commands_, &AccountCommands::requestActivateAliasServicing, worker_,
        &SessionWorker::activateAliasServicing);
    connect(&commands_, &AccountCommands::requestSetSync, worker_, &SessionWorker::setSyncEnabled);
    connect(&commands_, &AccountCommands::requestRebuildI2p, worker_,
        &SessionWorker::rebuildI2pLinks);
    connect(&commands_, &AccountCommands::requestCancelTransfer, worker_,
        &SessionWorker::cancelTransfer);
    connect(&commands_, &AccountCommands::requestGeneratePersonalKey, worker_,
        &SessionWorker::generatePersonalKey);
    connect(&commands_, &AccountCommands::requestLoadPersonalKey, worker_,
        &SessionWorker::loadPersonalKey);
    connect(&commands_, &AccountCommands::requestReplacePersonalKey, worker_,
        &SessionWorker::replacePersonalKey);
    connect(&commands_, &AccountCommands::requestSetAliasBinding, worker_,
        &SessionWorker::setAliasBinding);
    connect(&commands_, &AccountCommands::requestRetryRoutingTo, worker_,
        &SessionWorker::retryRoutingTo);
    connect(&commands_, &AccountCommands::requestPublishPersonalDest, worker_,
        &SessionWorker::publishPersonalDest);
    connect(&commands_, &AccountCommands::requestSetDelegationDays, worker_,
        &SessionWorker::setDelegationDays);
    connect(&commands_, &AccountCommands::requestSetAcceptCalls, worker_,
        &SessionWorker::setAcceptCalls);
    connect(&commands_, &AccountCommands::requestSetSendReceipts, worker_,
        &SessionWorker::setSendReceipts);
    connect(&commands_, &AccountCommands::requestDisablePersonalDest, worker_,
        &SessionWorker::disablePersonalDest);
    connect(&commands_, &AccountCommands::requestRefreshI2pStatus, worker_,
        &SessionWorker::refreshI2pStatus);
    connect(&commands_, &AccountCommands::requestRefreshStorageUsage, worker_,
        &SessionWorker::refreshStorageUsage);
    connect(&commands_, &AccountCommands::requestStartCall, worker_, &SessionWorker::startCall);
    connect(&commands_, &AccountCommands::requestAcceptCall, worker_, &SessionWorker::acceptCall);
    connect(&commands_, &AccountCommands::requestDeclineCall, worker_, &SessionWorker::declineCall);
    connect(&commands_, &AccountCommands::requestEndCall, worker_, &SessionWorker::endCall);
    connect(&commands_, &AccountCommands::requestSetCallMuted, worker_,
        &SessionWorker::setCallMuted);
    connect(&commands_, &AccountCommands::requestAckPending, worker_, &SessionWorker::ackPending);
    connect(&commands_, &AccountCommands::requestForgetPendingAdd, worker_,
        &SessionWorker::forgetPendingAdd);
    connect(&commands_, &AccountCommands::requestRefreshDevices, worker_,
        &SessionWorker::refreshDevices);
    connect(&commands_, &AccountCommands::requestForgetDevice, worker_,
        &SessionWorker::forgetDevice);
    connect(&commands_, &AccountCommands::requestCloseAccountOnServer, worker_,
        &SessionWorker::closeAccountOnServer);
    connect(&commands_, &AccountCommands::requestSignLoginSig, this, &CoreAccount::signLogin);
    connect(&commands_, &AccountCommands::requestShutdown, this, &CoreAccount::beginShutdown);

    connect(worker_, &SessionWorker::connectProgress, &events_, &AccountEvents::connectProgress);
    connect(worker_, &SessionWorker::addressMismatch, &events_, &AccountEvents::addressMismatch);
    connect(worker_, &SessionWorker::stopped, &events_, &AccountEvents::stopped);
    connect(worker_, &SessionWorker::downloadProgress, &events_, &AccountEvents::downloadProgress);
    connect(worker_, &SessionWorker::servedProgress, &events_, &AccountEvents::servedProgress);
    connect(worker_, &SessionWorker::transferStage, &events_, &AccountEvents::transferStage);
    connect(worker_, &SessionWorker::servedFinished, &events_, &AccountEvents::servedFinished);
    connect(worker_, &SessionWorker::downloadFinished, &events_, &AccountEvents::downloadFinished);
    connect(worker_, &SessionWorker::pairOfferReady, &events_, &AccountEvents::pairOfferReady);
    connect(worker_, &SessionWorker::pairStage, &events_, &AccountEvents::pairStage);
    connect(worker_, &SessionWorker::pairFinished, &events_, &AccountEvents::pairFinished);
    connect(worker_, &SessionWorker::aliasHoldings, &events_, &AccountEvents::aliasHoldings);
    connect(worker_, &SessionWorker::aliasActivationDone, &events_,
        &AccountEvents::aliasActivationDone);
    connect(worker_, &SessionWorker::opened, &events_, &AccountEvents::opened);
    connect(worker_, &SessionWorker::accountSettings, &events_, &AccountEvents::accountSettings);
    connect(worker_, &SessionWorker::renamed, &events_, &AccountEvents::renamed);
    connect(worker_, &SessionWorker::openFailed, &events_, &AccountEvents::openFailed);
    connect(worker_, &SessionWorker::connectionChanged, &events_,
        &AccountEvents::connectionChanged);
    connect(worker_, &SessionWorker::messageReceived, &events_, &AccountEvents::messageReceived);
    connect(worker_, &SessionWorker::contactsRefreshed, &events_,
        &AccountEvents::contactsRefreshed);
    connect(worker_, &SessionWorker::avatarReady, &events_, &AccountEvents::avatarReady);
    connect(worker_, &SessionWorker::sendProgress, &events_, &AccountEvents::sendProgress);
    connect(worker_, &SessionWorker::uploadProgress, &events_, &AccountEvents::uploadProgress);
    connect(worker_, &SessionWorker::sendResult, &events_, &AccountEvents::sendResult);
    connect(worker_, &SessionWorker::sendPhase, &events_, &AccountEvents::sendPhase);
    connect(worker_, &SessionWorker::contactRequestSent, &events_,
        &AccountEvents::contactRequestSent);
    connect(worker_, &SessionWorker::contactAddStage, &events_, &AccountEvents::contactAddStage);
    connect(worker_, &SessionWorker::contactAddDone, &events_, &AccountEvents::contactAddDone);
    connect(worker_, &SessionWorker::routingTold, &events_, &AccountEvents::routingTold);
    connect(worker_, &SessionWorker::contactAlreadyKnown, &events_,
        &AccountEvents::contactAlreadyKnown);
    connect(worker_, &SessionWorker::contactAliasUnknown, &events_,
        &AccountEvents::contactAliasUnknown);
    connect(worker_, &SessionWorker::contactAddResolved, &events_,
        &AccountEvents::contactAddResolved);
    connect(worker_, &SessionWorker::contactRequestUnconfirmed, &events_,
        &AccountEvents::contactRequestUnconfirmed);
    connect(worker_, &SessionWorker::contactAddResumed, &events_,
        &AccountEvents::contactAddResumed);
    connect(worker_, &SessionWorker::contactAddRateLimited, &events_,
        &AccountEvents::contactAddRateLimited);
    connect(worker_, &SessionWorker::contactAccepted, &events_, &AccountEvents::contactAccepted);
    connect(worker_, &SessionWorker::opBegin, &events_, &AccountEvents::opBegin);
    connect(worker_, &SessionWorker::opDone, &events_, &AccountEvents::opDone);
    connect(worker_, &SessionWorker::opProgress, &events_, &AccountEvents::opProgress);
    connect(worker_, &SessionWorker::syncReachable, &events_, &AccountEvents::syncReachable);
    connect(worker_, &SessionWorker::approvalState, &events_, &AccountEvents::approvalState);
    connect(worker_, &SessionWorker::devicesReady, &events_, &AccountEvents::devicesReady);
    connect(worker_, &SessionWorker::accountClosed, &events_, &AccountEvents::accountClosed);
    connect(worker_, &SessionWorker::facadeInfo, &events_, &AccountEvents::facadeInfo);
    connect(worker_, &SessionWorker::actionOk, &events_, &AccountEvents::actionOk);
    connect(worker_, &SessionWorker::botActionDone, &events_, &AccountEvents::botActionDone);
    connect(worker_, &SessionWorker::actionFailed, &events_, &AccountEvents::actionFailed);
    connect(worker_, &SessionWorker::inviteReady, &events_, &AccountEvents::inviteReady);
    connect(worker_, &SessionWorker::inviteUnavailable, &events_,
        &AccountEvents::inviteUnavailable);
    connect(worker_, &SessionWorker::loginSigned, &events_, &AccountEvents::loginSigned);
    connect(worker_, &SessionWorker::connectionLogReady, &events_,
        &AccountEvents::connectionLogReady);
    connect(worker_, &SessionWorker::serverHello, &events_, &AccountEvents::serverHello);
    connect(worker_, &SessionWorker::i2pStatus, &events_, &AccountEvents::i2pStatus);
    connect(worker_, &SessionWorker::i2pKeyState, &events_, &AccountEvents::i2pKeyState);
    connect(worker_, &SessionWorker::i2pServedAddress, &events_, &AccountEvents::i2pServedAddress);
    connect(worker_, &SessionWorker::storageUsageReady, &events_,
        &AccountEvents::storageUsageReady);
    connect(worker_, &SessionWorker::callStateChanged, &events_, &AccountEvents::callStateChanged);
    connect(worker_, &SessionWorker::callLogged, &events_, &AccountEvents::callLogged);
    connect(worker_, &SessionWorker::commandFinished, &events_, &AccountEvents::commandFinished);
    connect(worker_, &SessionWorker::loginSignerReady, this,
        [this](std::shared_ptr<bazarish::client::LoginSigner> signer) {
            loginSigner_ = std::move(signer);
        });
    connect(worker_, &SessionWorker::stopped, &thread_, &QThread::quit);
    connect(&thread_, &QThread::finished, &events_, &AccountEvents::closed);

    const QMetaObject& meta = AccountCommands::staticMetaObject;
    const QMetaMethod done = SessionWorker::staticMetaObject.method(
        SessionWorker::staticMetaObject.indexOfSlot("noteCommandDone()"));
    for (int i = meta.methodOffset(); i < meta.methodCount(); ++i) {
        const QMetaMethod method = meta.method(i);
        if (method.methodType() == QMetaMethod::Signal
            && !kSelfDescribingCommands.contains(method.name())) {
            connect(&commands_, method, worker_, done);
        }
    }

    thread_.start();
}

CoreAccount::~CoreAccount()
{
    shutdown();
}

void CoreAccount::shutdown()
{
    if (thread_.isRunning()) {
        thread_.quit();
        thread_.wait();
    }
}

void CoreAccount::beginShutdown()
{
    if (!thread_.isRunning()) {
        emit events_.closed();
        return;
    }
    QMetaObject::invokeMethod(worker_, &SessionWorker::shutdown, Qt::QueuedConnection);
}

void CoreAccount::signLogin(const QString& challenge)
{
    if (!loginSigner_) {
        QMetaObject::invokeMethod(
            worker_, [worker = worker_, challenge]() { worker->signLogin(challenge); },
            Qt::QueuedConnection);
        return;
    }
    QString blob;
    try {
        blob = QString::fromStdString(loginSigner_->sign(challenge.toStdString()));
    } catch (const std::exception& error) {
        const QString problem = QString::fromUtf8(error.what());
        QMetaObject::invokeMethod(
            this, [this, problem]() { emit events_.actionFailed(problem); }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(
        this, [this, blob]() { emit events_.loginSigned(blob); }, Qt::QueuedConnection);
}

}  // namespace bazarish::app
