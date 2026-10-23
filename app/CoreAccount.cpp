// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "DeliveryStatus.hpp"
#include "OperationLog.hpp"
#include "PathText.hpp"
#include "SessionShared.hpp"
#include "SessionWorker.hpp"
#include "SystemNotes.hpp"

#include "AccountDb.hpp"
#include "AccountKey.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QPointer>

#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace bazarish::app {

namespace {

const int kLoginSignerMetaType
    = qRegisterMetaType<std::shared_ptr<bazarish::client::LoginSigner>>(
        "std::shared_ptr<bazarish::client::LoginSigner>");
const int kContactStateMetaType
    = qRegisterMetaType<QVector<ContactState>>("QVector<ContactState>");

constexpr int kReadSyncIdleMs = 4000;
// How often every chat is looked through for messages whose auto-delete time ran out.
constexpr int kExpirySweepMs = 5 * 60 * 1000;
// A reconnect this client asked for is not reported before this much of it.
constexpr int kLinkRebuildGraceMs = 1500;
constexpr int kCommandTickMs = 200;
constexpr qint64 kCommandVisibleAfterMs = 400;

const char* commandTitle(const QByteArray& signalName)
{
    static const QHash<QByteArray, const char*> kNamed = {
        {"requestSendReceipt", QT_TR_NOOP("Confirming a message was read")},
        {"requestAckPending", QT_TR_NOOP("Clearing a message from the mailbox")},
        {"requestSendReaction", QT_TR_NOOP("Sending a reaction")},
        {"requestSendEdit", QT_TR_NOOP("Sending an edit")},
        {"requestSendDelete", QT_TR_NOOP("Deleting a message for both sides")},
        {"requestUnsend", QT_TR_NOOP("Withdrawing a file")},
        {"requestAcceptContact", QT_TR_NOOP("Agreeing to a contact request")},
        {"requestEmitSettings", QT_TR_NOOP("Telling your other devices")},
        {"requestSyncRead", QT_TR_NOOP("Marking a chat read")},
        {"requestSyncChatPin", QT_TR_NOOP("Pinning a chat")},
        {"requestSyncChatClear", QT_TR_NOOP("Clearing a chat")},
        {"requestClearChatForEveryone", QT_TR_NOOP("Clearing a chat for both sides")},
        {"requestSendChatTimer", QT_TR_NOOP("Setting the auto-delete timer")},
        {"requestContactsFromDevices", QT_TR_NOOP("Asking your other devices")},
        {"requestSetSync", QT_TR_NOOP("Going online")},
        {"requestRebuildI2p", QT_TR_NOOP("Rebuilding the I2P destinations")},
        {"requestInviteSig", QT_TR_NOOP("Preparing your invite")},
        {"requestSignLoginSig", QT_TR_NOOP("Signing in")},
        {"requestSetDisplayName", QT_TR_NOOP("Changing your name")},
        {"requestRenameContact", QT_TR_NOOP("Renaming a contact")},
        {"requestRemoveContact", QT_TR_NOOP("Deleting a contact")},
        {"requestClearSaved", QT_TR_NOOP("Clearing saved messages")},
        {"requestSetBlocked", QT_TR_NOOP("Updating the block list")},
        {"requestSetContactNotifications", QT_TR_NOOP("Changing a contact's notifications")},
        {"requestSetContactCalls", QT_TR_NOOP("Changing whether a contact may call")},
        {"requestConnectionLog", QT_TR_NOOP("Reading the connection log")},
        {"requestClearConnectionLog", QT_TR_NOOP("Clearing the connection log")},
        {"requestStartPairing", QT_TR_NOOP("Preparing a link for a new device")},
        {"requestStopPairing", QT_TR_NOOP("Closing the link for a new device")},
        {"requestChangePassphrase", QT_TR_NOOP("Changing the database password")},
        {"requestCancelTransfer", QT_TR_NOOP("Cancelling a file transfer")},
        {"requestRetryRoutingTo", QT_TR_NOOP("Sending your address to a contact")},
        {"requestSetAcceptCalls", QT_TR_NOOP("Changing incoming calls")},
        {"requestSetSendReceipts", QT_TR_NOOP("Changing read receipts")},
        {"requestSetDelegationDays", QT_TR_NOOP("Changing the delegation term")},
        {"requestDisablePersonalDest", QT_TR_NOOP("Taking your address offline")},
        {"requestForgetPendingAdd", QT_TR_NOOP("Dropping a contact request")},
        {"requestStartCall", QT_TR_NOOP("Starting a call")},
        {"requestAcceptCall", QT_TR_NOOP("Answering a call")},
        {"requestDeclineCall", QT_TR_NOOP("Declining a call")},
        {"requestEndCall", QT_TR_NOOP("Ending a call")},
        {"requestSetCallMuted", QT_TR_NOOP("Switching the microphone on or off")},
    };
    const auto found = kNamed.constFind(signalName);
    if (found == kNamed.cend()) {
        throw std::logic_error(
            "the background-task panel has no title for " + signalName.toStdString());
    }
    return found.value();
}

constexpr int kReactionsToFlash = 64;
QString loginStageText(const client::FetchStage stage)
{
    switch (stage) {
    case client::FetchStage::eTakingDest:
        return CoreAccount::tr("Taking an address to send from…");
    case client::FetchStage::eBuildingDest:
        return CoreAccount::tr("Building an address to send from…");
    case client::FetchStage::eReaching:
        return CoreAccount::tr("Reaching the site…");
    case client::FetchStage::eWaiting:
        return CoreAccount::tr("Waiting for the site's answer…");
    case client::FetchStage::eAskingAgain:
        return CoreAccount::tr("No answer; asking again…");
    }
    throw std::logic_error("no words for this sign-in stage");
}

// A sign-in step reaches a site over I2P off the core thread; its stages and its end come back.
template <typename Work, typename Stage, typename Done>
void overI2p(Work work, Stage stage, Done done)
{
    std::thread([work, stage, done]() {
        client::tellFetchStages([stage](const client::FetchStage reached) {
            QMetaObject::invokeMethod(
                QCoreApplication::instance(), [stage, reached]() { stage(reached); },
                Qt::QueuedConnection);
        });
        decltype(work()) result;
        QString problem;
        try {
            result = work();
        } catch (const std::exception& error) {
            problem = QString::fromUtf8(error.what());
        }
        client::tellFetchStages(nullptr);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [done, result, problem]() { done(result, problem); },
            Qt::QueuedConnection);
    }).detach();
}

constexpr int kHttpAcceptedFirst = 200;
constexpr int kHttpAcceptedLast = 299;

}  // namespace

CoreAccount::CoreAccount(const QString& accountId, QObject* const parent)
    : QObject(parent)
    , accountId_(accountId)
{
    worker_ = new SessionWorker();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);

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
    connect(&commands_, &AccountCommands::requestSendChatTimer, worker_,
        &SessionWorker::sendChatTimer);
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
    connect(&commands_, &AccountCommands::sendLogin, this, &CoreAccount::sendLogin);
    connect(&commands_, &AccountCommands::fetchLogin, this, &CoreAccount::fetchLogin);
    connect(&commands_, &AccountCommands::cancelLoginFetch, this, &CoreAccount::cancelLoginFetch);
    connect(&commands_, &AccountCommands::viewConversation, this, &CoreAccount::viewConversation);
    connect(&commands_, &AccountCommands::markRead, this, &CoreAccount::markRead);
    connect(&commands_, &AccountCommands::forgetReactionFlash, this,
        &CoreAccount::forgetReactionFlash);
    connect(&commands_, &AccountCommands::requestSetSendReceipts, this,
        [this](const bool on) { sendReceipts_ = on; });
    connect(&commands_, &AccountCommands::requestChangePassphrase, this,
        [this](const QString& passphrase) { passphrase_ = passphrase; });

    connect(worker_, &SessionWorker::connectProgress, &events_, &AccountEvents::connectProgress);
    connect(worker_, &SessionWorker::addressMismatch, &events_, &AccountEvents::addressMismatch);
    connect(worker_, &SessionWorker::downloadProgress, &events_, &AccountEvents::downloadProgress);
    connect(worker_, &SessionWorker::servedProgress, &events_, &AccountEvents::servedProgress);
    connect(worker_, &SessionWorker::transferStage, &events_, &AccountEvents::transferStage);
    connect(worker_, &SessionWorker::servedFinished, &events_, &AccountEvents::servedFinished);
    connect(worker_, &SessionWorker::downloadFinished, &events_, &AccountEvents::downloadFinished);
    connect(worker_, &SessionWorker::downloadFinished, this, &CoreAccount::onDownloadFinished);
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
    connect(worker_, &SessionWorker::messageReceived, this, &CoreAccount::onMessageReceived);
    connect(worker_, &SessionWorker::contactsRefreshed, &events_,
        &AccountEvents::contactsRefreshed);
    connect(worker_, &SessionWorker::avatarReady, &events_, &AccountEvents::avatarReady);
    connect(worker_, &SessionWorker::sendProgress, this, &CoreAccount::onSendProgress);
    connect(worker_, &SessionWorker::uploadProgress, &events_, &AccountEvents::uploadProgress);
    connect(worker_, &SessionWorker::sendResult, this, &CoreAccount::onSendResult);
    connect(worker_, &SessionWorker::sendPhase, this, &CoreAccount::onSendPhase);
    connect(worker_, &SessionWorker::contactRequestSent, this, &CoreAccount::onContactRequestSent);
    connect(worker_, &SessionWorker::contactAddStage, this, &CoreAccount::onContactAddStage);
    connect(worker_, &SessionWorker::contactAddDone, this, &CoreAccount::onContactAddDone);
    connect(worker_, &SessionWorker::routingTold, this, &CoreAccount::onRoutingTold);
    connect(worker_, &SessionWorker::contactAlreadyKnown, this,
        &CoreAccount::onContactAlreadyKnown);
    connect(worker_, &SessionWorker::contactAliasUnknown, this,
        &CoreAccount::onContactAliasUnknown);
    connect(worker_, &SessionWorker::contactAddResolved, this, &CoreAccount::onContactAddResolved);
    connect(worker_, &SessionWorker::contactRequestUnconfirmed, this,
        &CoreAccount::onContactRequestUnconfirmed);
    connect(worker_, &SessionWorker::contactAddResumed, this, &CoreAccount::onContactAddResumed);
    connect(worker_, &SessionWorker::contactAddRateLimited, this,
        &CoreAccount::onContactAddRateLimited);
    connect(worker_, &SessionWorker::contactAccepted, &events_, &AccountEvents::contactAccepted);
    connect(worker_, &SessionWorker::opBegin, &log_,
        [this](const QString& id, const QString& kind, const QString& title,
            const QString& status) { log_.begin(id, kind, title, status); });
    connect(worker_, &SessionWorker::opDone, &log_, &OperationLog::finish);
    connect(worker_, &SessionWorker::opProgress, &log_,
        [this](const QString& id, const QString& status) { log_.update(id, status); });
    connect(worker_, &SessionWorker::approvalState, &events_, &AccountEvents::approvalState);
    connect(worker_, &SessionWorker::devicesReady, &events_, &AccountEvents::devicesReady);
    connect(worker_, &SessionWorker::accountClosed, &events_, &AccountEvents::accountClosed);
    connect(worker_, &SessionWorker::facadeInfo, &events_, &AccountEvents::facadeInfo);
    connect(worker_, &SessionWorker::actionOk, &events_, &AccountEvents::actionOk);
    connect(worker_, &SessionWorker::botActionDone, &log_,
        [this](const QString& id, const bool ok, const QString& error) {
            log_.finish(id, ok, ok ? tr("Sent") : error);
        });
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
    connect(worker_, &SessionWorker::callStateChanged, this, &CoreAccount::onCallStateChanged);
    connect(worker_, &SessionWorker::callLogged, this, &CoreAccount::onCallLogged);
    connect(worker_, &SessionWorker::commandFinished, this, &CoreAccount::onCommandFinished);
    connect(worker_, &SessionWorker::loginSignerReady, this,
        [this](std::shared_ptr<bazarish::client::LoginSigner> signer) {
            loginSigner_ = std::move(signer);
        });
    connect(worker_, &SessionWorker::opened, this,
        [this](const QString& fingerprint, const QString& displayName, const bool connected) {
            contacts_.setOwnFingerprint(fingerprint);
            displayName_ = displayName;
            online_ = connected && startOnline_;
            announcePresence();
        });
    connect(worker_, &SessionWorker::renamed, this, [this](const QString& name) {
        displayName_ = name;
        emit changed();
    });
    connect(&ingest_, &MessageIngest::accountRenamed, this, [this](const QString& name) {
        displayName_ = name;
        emit changed();
    });
    connect(worker_, &SessionWorker::connectionChanged, this,
        [this](const bool connected, const QString&) {
            online_ = connected;
            announcePresence();
        });
    connect(worker_, &SessionWorker::syncReachable, this, &CoreAccount::onSyncReachable);
    connect(worker_, &SessionWorker::facadeInfo, this,
        [this](const QString& activeUrl, const QStringList& configured) {
            activeFacade_ = activeUrl;
            hasServer_ = !configured.isEmpty();
            emit changed();
        });
    rebuildGrace_.setSingleShot(true);
    rebuildGrace_.setInterval(kLinkRebuildGraceMs);
    connect(&rebuildGrace_, &QTimer::timeout, this, [this]() {
        linksRebuilding_ = false;
        onSyncReachable(false);
    });
    connect(worker_, &SessionWorker::accountSettings, this,
        [this](const bool, const bool sendReceipts) { sendReceipts_ = sendReceipts; });
    connect(worker_, &SessionWorker::contactsRefreshed, this,
        [this](const QVector<ContactState>& contacts, const QStringList& blocked) {
            contacts_.reset(contacts, blocked);
            syncAgreeingRows();
        });
    connect(worker_, &SessionWorker::stopped, this, &CoreAccount::close);
    connect(worker_, &SessionWorker::stopped, &thread_, &QThread::quit);

    connect(&ingest_, &MessageIngest::rowAdded, &events_, &AccountEvents::rowAdded);
    connect(&ingest_, &MessageIngest::rowChanged, &events_, &AccountEvents::rowChanged);
    connect(&ingest_, &MessageIngest::rowRemoved, &events_, &AccountEvents::rowRemoved);
    connect(&ingest_, &MessageIngest::peerReset, &events_, &AccountEvents::peerReset);
    connect(&ingest_, &MessageIngest::chatTimerChanged, &events_,
        &AccountEvents::chatTimerChanged);
    connect(&ingest_, &MessageIngest::botMenuChanged, &events_, &AccountEvents::botMenuChanged);
    connect(&ingest_, &MessageIngest::peerForgotten, &events_, &AccountEvents::peerForgotten);
    connect(&ingest_, &MessageIngest::chatChanged, &events_, &AccountEvents::chatChanged);
    connect(&ingest_, &MessageIngest::chatsChanged, &events_, &AccountEvents::chatsChanged);
    connect(&ingest_, &MessageIngest::deliveredThrough, this, &CoreAccount::noteDeliveredThrough);
    connect(&ingest_, &MessageIngest::deliveredThrough, &events_,
        &AccountEvents::deliveredThrough);
    connect(&ingest_, &MessageIngest::reactionsChanged, &events_,
        &AccountEvents::reactionsChanged);
    connect(&ingest_, &MessageIngest::groupChanged, &events_, &AccountEvents::groupChanged);
    connect(&ingest_, &MessageIngest::accountRenamed, &events_, &AccountEvents::accountRenamed);
    connect(&ingest_, &MessageIngest::contactsRevised, &events_,
        &AccountEvents::contactsRevised);
    connect(&ingest_, &MessageIngest::settingsAsked, &commands_,
        &AccountCommands::requestEmitSettings);
    connect(&ingest_, &MessageIngest::readElsewhere, this, [this](const QString& peer) {
        lastReadAckedId_[peer] = qMax(lastReadAckedId_.value(peer, 0), store_.lastReadId(peer));
        emit events_.chatChanged(peer);
    });
    connect(&ingest_, &MessageIngest::editArrived, this,
        [this](const QString& peer, const qint64 localId, const QString& ref) {
            if (peer == viewedPeer_ && sendReceipts_
                && localId <= lastReadAckedId_.value(peer, 0)) {
                emit commands_.requestSendReceipt(peer, ref);
            }
        });
    connect(&ingest_, &MessageIngest::messageArrived, this, [this](const QString& peer) {
        if (contacts_.state(peer).notifications) {
            emit messageNotification(peer, contacts_.peerName(peer));
        }
    });
    connect(&ingest_, &MessageIngest::reactionArrived, this,
        [this](const QString& peer, const QString& target, const QString& emoji) {
            noteReactionToFlash(peer, target);
            if (contacts_.state(peer).notifications) {
                emit reactionNotification(peer, contacts_.peerName(peer), emoji);
            }
        });

    for (const auto rowEvent : {&AccountEvents::peerReset, &AccountEvents::peerForgotten,
             &AccountEvents::chatChanged}) {
        connect(&events_, rowEvent, this, &CoreAccount::recountUnread);
    }
    connect(&events_, &AccountEvents::rowAdded, this, &CoreAccount::recountUnread);
    connect(&events_, &AccountEvents::rowRemoved, this, &CoreAccount::recountUnread);
    connect(&events_, &AccountEvents::chatsChanged, this, &CoreAccount::recountUnread);
    connect(&log_, &OperationLog::changed, &events_, &AccountEvents::opChanged);
    connect(&log_, &OperationLog::removed, &events_, &AccountEvents::opRemoved);
    connect(&commands_, &AccountCommands::sendText, this, &CoreAccount::sendText);
    connect(&commands_, &AccountCommands::sendFile, this, &CoreAccount::sendFile);
    connect(&commands_, &AccountCommands::sendPicture, this, &CoreAccount::sendPicture);
    connect(&commands_, &AccountCommands::sendVoice, this, &CoreAccount::sendVoice);
    connect(&commands_, &AccountCommands::forwardMessage, this, &CoreAccount::forwardMessage);
    connect(&commands_, &AccountCommands::resend, this, &CoreAccount::resend);
    connect(&commands_, &AccountCommands::editMessage, this, &CoreAccount::editMessage);
    connect(&commands_, &AccountCommands::deleteMessage, this, &CoreAccount::deleteMessage);
    connect(&commands_, &AccountCommands::react, this, &CoreAccount::react);
    connect(&commands_, &AccountCommands::pinChat, this, &CoreAccount::pinChat);
    connect(&commands_, &AccountCommands::clearChat, this, &CoreAccount::clearChat);
    connect(&commands_, &AccountCommands::setChatTimer, this, &CoreAccount::setChatTimer);
    connect(&commands_, &AccountCommands::forgetChat, this, &CoreAccount::forgetChat);
    connect(&commands_, &AccountCommands::clearSavedEverywhere, this,
        &CoreAccount::clearSavedEverywhere);
    connect(&commands_, &AccountCommands::addByInvite, this, &CoreAccount::addByInvite);
    connect(&commands_, &AccountCommands::addByAlias, this, &CoreAccount::addByAlias);
    connect(&commands_, &AccountCommands::retryContactAdd, this, &CoreAccount::retryContactAdd);
    connect(&commands_, &AccountCommands::retryContactRequest, this,
        &CoreAccount::retryContactRequest);
    connect(&commands_, &AccountCommands::declineCall, this, &CoreAccount::declineCall);
    connect(&commands_, &AccountCommands::endCall, this, &CoreAccount::endCall);
    connect(&commands_, &AccountCommands::sendCallback, this, &CoreAccount::sendCallback);
    connect(&commands_, &AccountCommands::sendCommand, this, &CoreAccount::sendCommand);
    connect(&commands_, &AccountCommands::saveAttachment, this, &CoreAccount::saveAttachment);
    connect(&commands_, &AccountCommands::trim, this, &CoreAccount::trim);
    connect(&commands_, &AccountCommands::compact, this, &CoreAccount::compact);
    connect(&commands_, &AccountCommands::requestConnect, this, &CoreAccount::onConnectRequested);
    connect(&commands_, &AccountCommands::requestActivateAliasServicing, this, [this]() {
        log_.begin(kAliasOperationId, QStringLiteral("alias"), tr("Checking your aliases"),
            tr("Asking the registry over I2P…"));
    });
    connect(&commands_, &AccountCommands::requestExport, this, [this]() {
        log_.begin(QStringLiteral("export"), QStringLiteral("account"),
            tr("Exporting your backup"), tr("Waiting for this account…"));
    });
    connect(&commands_, &AccountCommands::requestGeneratePersonalKey, this, [this]() {
        log_.begin(QStringLiteral("dest-key"), QStringLiteral("dest"),
            tr("Creating your destination key"), tr("Queued…"));
    });
    connect(worker_, &SessionWorker::connectProgress, this, &CoreAccount::onConnectProgress);
    connect(worker_, &SessionWorker::connectionChanged, this, &CoreAccount::settleConnect);
    connect(worker_, &SessionWorker::actionFailed, this,
        [this](const QString& reason) { settleConnect(false, reason); });
    connect(worker_, &SessionWorker::aliasActivationDone, this,
        [this](const bool ok, const QString& text) { log_.finish(kAliasOperationId, ok, text); });
    connect(worker_, &SessionWorker::uploadProgress, this, &CoreAccount::onUploadProgress);
    connect(worker_, &SessionWorker::downloadProgress, this, &CoreAccount::onDownloadProgress);
    connect(worker_, &SessionWorker::transferStage, this, &CoreAccount::onTransferStage);
    connect(worker_, &SessionWorker::servedProgress, this, &CoreAccount::onServedProgress);
    connect(worker_, &SessionWorker::servedFinished, this, &CoreAccount::onServedFinished);
    commandTimer_.setInterval(kCommandTickMs);
    connect(&commandTimer_, &QTimer::timeout, this, &CoreAccount::showSlowCommands);

    readSyncTimer_.setSingleShot(true);
    connect(&readSyncTimer_, &QTimer::timeout, this, &CoreAccount::flushReadSync);
    expiryTimer_.setInterval(kExpirySweepMs);
    connect(&expiryTimer_, &QTimer::timeout, this, &CoreAccount::sweepExpired);
    connect(&thread_, &QThread::finished, &events_, &AccountEvents::closed);

    const QMetaObject& meta = AccountCommands::staticMetaObject;
    const QMetaMethod noted
        = metaObject()->method(metaObject()->indexOfSlot("noteCommandQueued()"));
    const QMetaMethod done = SessionWorker::staticMetaObject.method(
        SessionWorker::staticMetaObject.indexOfSlot("noteCommandDone()"));
    for (int i = meta.methodOffset(); i < meta.methodCount(); ++i) {
        const QMetaMethod method = meta.method(i);
        if (listedWhileQueued(method)) {
            commandTitles_.insert(i, commandTitle(method.name()));
            connect(&commands_, method, this, noted);
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
    close();
}

void CoreAccount::networkChanged()
{
    worker_->networkChanged();
}

void CoreAccount::goOnline()
{
    startOnline_ = true;
    online_ = true;
    announcePresence();
    emit commands_.requestSetSync(true);
}

void CoreAccount::goOffline()
{
    online_ = false;
    reachable_ = false;
    announcePresence();
    emit commands_.requestSetSync(false);
}

void CoreAccount::replayOperations()
{
    for (const OperationRow& row : log_.rows()) {
        emit events_.opChanged(row);
    }
}

void CoreAccount::rebuildI2pLinks()
{
    linksRebuilding_ = true;
    rebuildGrace_.start();
    emit commands_.requestRebuildI2p();
}

void CoreAccount::onSyncReachable(const bool ok)
{
    if (ok) {
        linksRebuilding_ = false;
        rebuildGrace_.stop();
    } else if (linksRebuilding_) {
        return;
    }
    if (reachable_ == ok) {
        return;
    }
    reachable_ = ok;
    announcePresence();
}

void CoreAccount::recountUnread()
{
    const int total = store_.unreadTotal();
    if (total == unread_) {
        return;
    }
    unread_ = total;
    emit changed();
}

void CoreAccount::announcePresence()
{
    emit events_.presence(online_, reachable_);
    emit changed();
}

void CoreAccount::open(const QString& file, const QString& passphrase, const bool startOnline)
{
    accountPath_ = file;
    passphrase_ = passphrase;
    startOnline_ = startOnline;
    if (!store_.open({}, file, passphrase)
        || !store_.failUnsentOnLoad(
            DeliveryStatus::Preparing, DeliveryStatus::Delivering, DeliveryStatus::Failed)
        || !store_.settleUnfinishedNotes(QStringLiteral("system"), DeliveryStatus::Preparing,
            DeliveryStatus::Received,
            encodeSystemNote(QT_TR_NOOP("The contact request did not finish.")))) {
        emit events_.openFailed(tr("This profile could not be opened."));
        return;
    }
    unread_ = store_.unreadTotal();
    sweepExpired();
    expiryTimer_.start();
    const Bytes key = client::accountkey::keyFor(toPath(file), passphrase.toStdString());
    emit events_.transcriptOpened(file,
        QByteArray(reinterpret_cast<const char*>(key.data()), static_cast<qsizetype>(key.size())));
    const QJsonDocument flashes = QJsonDocument::fromJson(
        QByteArray::fromStdString(accountDb().text("reactions-to-flash")));
    for (const QJsonValue& entry : flashes.array()) {
        reactionsToFlash_ << entry.toString();
    }
    emit events_.reactionsToFlash(reactionsToFlash_);
    const QJsonDocument recents = QJsonDocument::fromJson(
        QByteArray::fromStdString(accountDb().text("recent-reactions")));
    for (const QJsonValue& entry : recents.array()) {
        recentReactions_ << entry.toString();
    }
    emit events_.recentReactions(recentReactions_);
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, file, passphrase, startOnline]() {
            worker->openAccount(file, passphrase, startOnline);
        },
        Qt::QueuedConnection);
}

void CoreAccount::close()
{
    expiryTimer_.stop();
    accountDb_.reset();
    store_.close();
}

client::AccountDb& CoreAccount::accountDb()
{
    if (!accountDb_) {
        accountDb_ = std::make_unique<client::AccountDb>(
            toPath(accountPath_), passphrase_.toStdString());
    }
    return *accountDb_;
}

void CoreAccount::reportUnsaved()
{
    emit events_.actionFailed(tr("History not saved: %1").arg(store_.lastError()));
}

void CoreAccount::onMessageReceived(const QVariantMap& message)
{
    if (!ingest_.takeIn(message)) {
        emit events_.actionFailed(tr("Receiving stopped: %1").arg(store_.lastError()));
        return;
    }
    if (const QString pendingId = message.value("pendingId").toString(); !pendingId.isEmpty()) {
        emit commands_.requestAckPending(pendingId);
    }
}

void CoreAccount::viewConversation(const QString& peer)
{
    flushReadSync();
    viewedPeer_ = peer;
    if (!peer.isEmpty()) {
        lastReadAckedId_[peer] = qMax(lastReadAckedId_.value(peer, 0), store_.lastReadId(peer));
        sweepExpired();
    }
}

void CoreAccount::markRead(
    const QString& peer, const qint64 id, const QString& e2eId, const qint64 sentAtMs)
{
    if (peer.isEmpty() || id <= lastReadAckedId_.value(peer, 0)) {
        return;
    }
    if (!store_.setLastReadId(peer, id)) {
        reportUnsaved();
        return;
    }
    lastReadAckedId_[peer] = id;
    emit events_.chatChanged(peer);
    pendingReadSync_[peer] = sentAtMs;
    readSyncTimer_.start(kReadSyncIdleMs);
    if (!sendReceipts_ || !online_) {
        return;
    }
    emit commands_.requestSendReceipt(peer, e2eId);
}

void CoreAccount::flushReadSync()
{
    readSyncTimer_.stop();
    if (!online_) {
        pendingReadSync_.clear();
        return;
    }
    for (auto it = pendingReadSync_.constBegin(); it != pendingReadSync_.constEnd(); ++it) {
        emit commands_.requestSyncRead(it.key(), it.value());
    }
    pendingReadSync_.clear();
}

void CoreAccount::noteReactionToFlash(const QString& peer, const QString& target)
{
    const QString key = reactionFlashKey(peer, target);
    reactionsToFlash_.removeAll(key);
    reactionsToFlash_.append(key);
    while (reactionsToFlash_.size() > kReactionsToFlash) {
        reactionsToFlash_.removeFirst();
    }
    persistReactionsToFlash();
}

void CoreAccount::forgetReactionFlash(const QString& peer)
{
    const QString prefix = reactionFlashKey(peer, {});
    const qsizetype before = reactionsToFlash_.size();
    reactionsToFlash_.removeIf(
        [&prefix](const QString& entry) { return entry.startsWith(prefix); });
    if (reactionsToFlash_.size() != before) {
        persistReactionsToFlash();
    }
}

void CoreAccount::persistReactionsToFlash()
{
    QJsonArray array;
    for (const QString& entry : reactionsToFlash_) {
        array.append(entry);
    }
    accountDb().putText("reactions-to-flash",
        QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString());
    emit events_.reactionsToFlash(reactionsToFlash_);
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

void CoreAccount::sendLogin(const QString& challenge)
{
    const qint64 startedAt = nowMillis();
    if (!loginSigner_) {
        emit events_.loginSendState(
            challenge, eLoginRefused, tr("This account is still opening."), startedAt);
        return;
    }
    const QString op = QStringLiteral("login:") + QString::number(++commandSeq_);
    const QString first = loginStageText(client::FetchStage::eTakingDest);
    log_.begin(op, QStringLiteral("login"), tr("Sending your signature"), first);
    emit events_.loginSendState(challenge, eLoginSending, first, startedAt);
    const std::shared_ptr<client::LoginSigner> signer = loginSigner_;
    const QPointer<CoreAccount> account(this);
    overI2p([signer, challenge]() { return signer->send(challenge.toStdString()); },
        [account, op, challenge, startedAt](const client::FetchStage stage) {
            if (account) {
                account->showLoginStage(op, challenge, startedAt, stage);
            }
        },
        [account, op, challenge, startedAt](
            const std::optional<int> status, const QString& problem) {
            if (account) {
                account->finishLoginSend(op, challenge, startedAt, status, problem);
            }
        });
}

void CoreAccount::showLoginStage(const QString& op, const QString& challenge,
    const qint64 startedAtMs, const client::FetchStage stage)
{
    if (!log_.has(op)) {
        return;
    }
    const QString text = loginStageText(stage);
    log_.update(op, text);
    emit events_.loginSendState(challenge, eLoginSending, text, startedAtMs);
}

void CoreAccount::finishLoginSend(const QString& op, const QString& challenge,
    const qint64 startedAtMs, const std::optional<int> status, const QString& problem)
{
    const bool accepted = problem.isEmpty() && status && *status >= kHttpAcceptedFirst
        && *status <= kHttpAcceptedLast;
    const QString said = !problem.isEmpty() ? problem
        : !status                          ? tr("The site did not answer over I2P")
        : accepted                         ? tr("Accepted")
                                           : tr("The site refused it (HTTP %1)").arg(*status);
    log_.finish(op, accepted, said);
    emit events_.loginSendState(
        challenge, accepted ? eLoginAccepted : eLoginRefused, said, startedAtMs);
}

void CoreAccount::fetchLogin(const QString& link)
{
    if (!loginSigner_) {
        emit events_.loginFetchState(link, false, tr("This account is still opening."), {});
        return;
    }
    cancelLoginFetch(loginFetchLink_);
    const QString op = QStringLiteral("login:") + QString::number(++commandSeq_);
    const QString first = loginStageText(client::FetchStage::eTakingDest);
    log_.begin(op, QStringLiteral("login"), tr("Getting the challenge"), first);
    emit events_.loginFetchState(link, true, first, {});
    loginFetchOp_ = op;
    loginFetchLink_ = link;
    loginFetchCancelled_ = std::make_shared<std::atomic<bool>>(false);
    const std::shared_ptr<client::LoginSigner> signer = loginSigner_;
    const std::shared_ptr<const std::atomic<bool>> cancelled = loginFetchCancelled_;
    const QPointer<CoreAccount> account(this);
    overI2p([signer, link, cancelled]() { return signer->fetch(link.toStdString(), *cancelled); },
        [account, op, link](const client::FetchStage stage) {
            if (account) {
                account->showLoginFetchStage(op, link, stage);
            }
        },
        [account, op, link](
            const std::optional<std::string>& challenge, const QString& problem) {
            if (account) {
                account->finishLoginFetch(op, link, challenge, problem);
            }
        });
}

void CoreAccount::cancelLoginFetch(const QString& link)
{
    if (!loginFetchCancelled_ || loginFetchLink_ != link) {
        return;
    }
    loginFetchCancelled_->store(true);
    loginFetchCancelled_.reset();
    log_.finish(loginFetchOp_, true, tr("Cancelled"));
    loginFetchOp_.clear();
    loginFetchLink_.clear();
    emit events_.loginFetchState(link, false, {}, {});
}

void CoreAccount::showLoginFetchStage(
    const QString& op, const QString& link, const client::FetchStage stage)
{
    if (op != loginFetchOp_ || !log_.has(op)) {
        return;
    }
    const QString text = loginStageText(stage);
    log_.update(op, text);
    emit events_.loginFetchState(link, true, text, {});
}

void CoreAccount::finishLoginFetch(const QString& op, const QString& link,
    const std::optional<std::string>& challenge, const QString& problem)
{
    if (op != loginFetchOp_) {
        return;
    }
    loginFetchOp_.clear();
    loginFetchLink_.clear();
    loginFetchCancelled_.reset();
    const bool received = problem.isEmpty() && challenge.has_value();
    const QString said = !problem.isEmpty() ? problem
        : !challenge                       ? tr("The site did not answer over I2P")
                                           : tr("Received");
    log_.finish(op, received, said);
    emit events_.loginFetchState(link, false, received ? QString() : said,
        received ? QString::fromStdString(*challenge) : QString());
}

void CoreAccount::noteCommandQueued()
{
    QueuedCommand command;
    command.id = QStringLiteral("cmd:") + QString::number(++commandSeq_);
    command.title = tr(commandTitles_.value(senderSignalIndex()));
    command.queuedAtMs = nowMillis();
    commandQueue_.append(command);
    if (!commandTimer_.isActive()) {
        commandTimer_.start();
    }
}

void CoreAccount::showSlowCommands()
{
    if (commandQueue_.isEmpty()) {
        commandTimer_.stop();
        return;
    }
    const qint64 now = nowMillis();
    for (int at = 0; at < commandQueue_.size(); ++at) {
        QueuedCommand& command = commandQueue_[at];
        if (now - command.queuedAtMs < kCommandVisibleAfterMs) {
            continue;
        }
        const QString status = at == 0 ? tr("Working on it…") : tr("Waiting its turn…");
        if (!command.shown) {
            command.shown = true;
            log_.begin(command.id, QStringLiteral("command"), command.title, status);
            continue;
        }
        log_.update(command.id, status);
    }
}

void CoreAccount::onCommandFinished()
{
    if (commandQueue_.isEmpty()) {
        return;
    }
    const QueuedCommand command = commandQueue_.takeFirst();
    if (command.shown) {
        log_.finish(command.id, true, tr("Done"));
    }
    if (commandQueue_.isEmpty()) {
        commandTimer_.stop();
    }
}

}  // namespace bazarish::app
