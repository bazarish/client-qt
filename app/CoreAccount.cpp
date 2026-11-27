// Bazarish project (c) 2026
#include "CoreAccount.hpp"
#include "Background.hpp"

#include "ChatPreview.hpp"
#include "DeliveryStatus.hpp"
#include "LinkFailureText.hpp"
#include "OperationLog.hpp"
#include "PathText.hpp"
#include "SessionShared.hpp"
#include "SessionWorker.hpp"
#include "SystemNotes.hpp"

#include "AccountDb.hpp"
#include "AccountKey.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <algorithm>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QPointer>

#include <exception>
#include <iterator>
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
const int kIncomingMessageMetaType = qRegisterMetaType<bazarish::client::IncomingMessage>(
    "bazarish::client::IncomingMessage");

constexpr int kReadSyncIdleMs = 4000;
// The newest rows a chat read from a notice looks through for the contact's last message.
constexpr int kReadScanRows = 50;
constexpr int kPresenceOrderSettleMs = 2000;
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
        {"requestSignLoginSig", QT_TR_NOOP("Signing in")},
        {"requestSetDisplayName", QT_TR_NOOP("Changing your name")},
        {"requestRenameContact", QT_TR_NOOP("Renaming a contact")},
        {"requestRemoveContact", QT_TR_NOOP("Deleting a contact")},
        {"requestClearSaved", QT_TR_NOOP("Clearing saved messages")},
        {"requestSetBlocked", QT_TR_NOOP("Updating the block list")},
        {"requestSetContactNotifications", QT_TR_NOOP("Changing a contact's notifications")},
        {"requestSetContactCalls", QT_TR_NOOP("Changing whether a contact may call")},
        {"requestClearConnectionLog", QT_TR_NOOP("Clearing the connection log")},
        {"requestStartPairing", QT_TR_NOOP("Preparing a link for a new device")},
        {"requestStopPairing", QT_TR_NOOP("Closing the link for a new device")},
        {"requestCancelTransfer", QT_TR_NOOP("Cancelling a file transfer")},
        {"requestRetryRoutingTo", QT_TR_NOOP("Sending your address to a contact")},
        {"requestSetAcceptCalls", QT_TR_NOOP("Changing incoming calls")},
        {"requestSetSendReceipts", QT_TR_NOOP("Changing read receipts")},
        {"requestSetShowPresence", QT_TR_NOOP("Changing online status")},
        {"requestSetDelegationDays", QT_TR_NOOP("Changing the delegation term")},
        {"requestDisablePersonalDest", QT_TR_NOOP("Taking your address offline")},
        {"requestForgetPendingAdd", QT_TR_NOOP("Dropping a contact request")},
        {"requestStartCall", QT_TR_NOOP("Starting a call")},
        {"requestAcceptCall", QT_TR_NOOP("Answering a call")},
        {"requestDeclineCall", QT_TR_NOOP("Declining a call")},
        {"requestEndCall", QT_TR_NOOP("Ending a call")},
        {"requestSetCallMuted", QT_TR_NOOP("Switching the microphone on or off")},
        {"requestAskGroupMembers", QT_TR_NOOP("Asking for the member list")},
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
        return CoreAccount::tr("Taking an address to send from...");
    case client::FetchStage::eBuildingDest:
        return CoreAccount::tr("Building an address to send from...");
    case client::FetchStage::eReaching:
        return CoreAccount::tr("Reaching the site...");
    case client::FetchStage::eWaiting:
        return CoreAccount::tr("Waiting for the site's answer...");
    }
    throw std::logic_error("no words for this sign-in stage");
}

// stage and done run on the application thread.
template <typename Work, typename Stage, typename Done>
void overI2p(Work work, Stage stage, Done done)
{
    inBackground([work, stage, done]() {
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
            bazarish::log::info("sign-in exchange failed: {}", error.what());
            problem = failureText(error, FarSide::eSite);
        }
        client::tellFetchStages(nullptr);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [done, result, problem]() { done(result, problem); },
            Qt::QueuedConnection);
    });
}

QString siteFailureText(const client::LinkFailure failure)
{
    return linkFailureText(static_cast<int>(failure), FarSide::eSite);
}

constexpr int kHttpAcceptedFirst = 200;
constexpr int kHttpAcceptedLast = 299;

}  // namespace

CoreAccount::CoreAccount(const QString& accountId, QObject* const parent)
    : QObject(parent)
    , accountId_(accountId)
{
    // ownership: allowed - deleted on its own thread once that thread finishes.
    worker_ = new SessionWorker();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(&commands_, &AccountCommands::requestConnect, worker_,
        &SessionWorker::connectAndRegister);
    connect(&commands_, &AccountCommands::requestSendText, worker_, &SessionWorker::sendText);
    connect(&commands_, &AccountCommands::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(&commands_, &AccountCommands::requestSendFileBytes, worker_,
        &SessionWorker::sendFileBytes);
    connect(&commands_, &AccountCommands::requestSendPicture, worker_, &SessionWorker::sendPicture);
    connect(&commands_, &AccountCommands::requestSendVoice, worker_, &SessionWorker::sendVoice);
    connect(&commands_, &AccountCommands::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(&commands_, &AccountCommands::watchUnsent, worker_, &SessionWorker::watchUnsent);
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
    connect(&commands_, &AccountCommands::requestSetStatus, worker_, &SessionWorker::setStatus);
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
    connect(&commands_, &AccountCommands::requestPublishThisDeviceAddress, worker_,
        &SessionWorker::publishThisDeviceAddress);
    connect(&commands_, &AccountCommands::requestPublishFreshAddress, worker_,
        &SessionWorker::publishFreshAddress);
    connect(&commands_, &AccountCommands::watchConnectionLog, worker_,
        &SessionWorker::watchConnectionLog);
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
    connect(&commands_, &AccountCommands::requestSetAliasWord, worker_,
        &SessionWorker::setAliasWord);
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
    connect(&commands_, &AccountCommands::requestSetShowPresence, worker_,
        &SessionWorker::setShowPresence);
    connect(&commands_, &AccountCommands::watchPresence, worker_, &SessionWorker::watchPresence);
    connect(&commands_, &AccountCommands::watchOverview, worker_, &SessionWorker::watchOverview);
    connect(&commands_, &AccountCommands::presenceTyped, worker_, &SessionWorker::presenceTyped);
    connect(&commands_, &AccountCommands::presenceStoppedTyping, worker_,
        &SessionWorker::presenceStoppedTyping);
    connect(worker_, &SessionWorker::peerPresence, &events_, &AccountEvents::peerPresence);
    connect(&commands_, &AccountCommands::requestDisablePersonalDest, worker_,
        &SessionWorker::disablePersonalDest);
    connect(&commands_, &AccountCommands::requestStartCall, worker_, &SessionWorker::startCall);
    connect(&commands_, &AccountCommands::requestAcceptCall, worker_, &SessionWorker::acceptCall);
    connect(&commands_, &AccountCommands::requestDeclineCall, worker_, &SessionWorker::declineCall);
    connect(&commands_, &AccountCommands::requestEndCall, worker_, &SessionWorker::endCall);
    connect(&commands_, &AccountCommands::requestSetCallMuted, worker_,
        &SessionWorker::setCallMuted);
    connect(&commands_, &AccountCommands::requestAckPending, worker_, &SessionWorker::ackPending);
    connect(&commands_, &AccountCommands::requestAskGroupMembers, worker_,
        &SessionWorker::askGroupMembers);
    connect(&commands_, &AccountCommands::requestForgetPendingAdd, worker_,
        &SessionWorker::forgetPendingAdd);
    connect(&commands_, &AccountCommands::requestRevokeDevice, worker_,
        &SessionWorker::revokeDevice);
    connect(&commands_, &AccountCommands::requestClearDeviceQueue, worker_,
        &SessionWorker::clearDeviceQueue);
    connect(&commands_, &AccountCommands::requestLeaveAccount, worker_,
        &SessionWorker::leaveAccount);
    connect(&commands_, &AccountCommands::requestReviewDevices, worker_,
        &SessionWorker::reviewDevices);
    connect(&commands_, &AccountCommands::readContactDevices, worker_,
        &SessionWorker::readContactDevices);
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

    connect(worker_, &SessionWorker::connectProgress, &events_, &AccountEvents::connectProgress);
    connect(worker_, &SessionWorker::addressMismatch, &events_, &AccountEvents::addressMismatch);
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
    connect(worker_, &SessionWorker::ownStatus, &events_, &AccountEvents::ownStatus);
    connect(worker_, &SessionWorker::openFailed, &events_, &AccountEvents::openFailed);
    connect(worker_, &SessionWorker::connectionChanged, &events_,
        &AccountEvents::connectionChanged);
    connect(worker_, &SessionWorker::messageReceived, this, &CoreAccount::onMessageReceived);
    connect(worker_, &SessionWorker::contactsRefreshed, &events_,
        &AccountEvents::contactsRefreshed);
    connect(worker_, &SessionWorker::avatarReady, &events_, &AccountEvents::avatarReady);
    connect(worker_, &SessionWorker::avatarReady, this,
        [this](const QString& fingerprint, const QByteArray& data) {
            if (data.isEmpty()) {
                avatars_.remove(fingerprint);
            } else {
                avatars_.insert(fingerprint, data);
            }
        });
    connect(worker_, &SessionWorker::sendProgress, this, &CoreAccount::onSendProgress);
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
    connect(worker_, &SessionWorker::contactAddResumed, this, &CoreAccount::onContactAddResumed);
    connect(worker_, &SessionWorker::contactAddRateLimited, this,
        &CoreAccount::onContactAddRateLimited);
    connect(worker_, &SessionWorker::contactAccepted, &events_, &AccountEvents::contactAccepted);
    connect(worker_, &SessionWorker::opBegin, &log_,
        [this](const QString& id, const QString& kind, const QString& title,
            const QString& status) { log_.begin(id, kind, title, status); });
    connect(worker_, &SessionWorker::opDone, &log_, &OperationLog::finish);
    connect(worker_, &SessionWorker::approvalState, &events_, &AccountEvents::approvalState);
    connect(worker_, &SessionWorker::overviewReady, &events_, &AccountEvents::overviewReady);
    connect(worker_, &SessionWorker::overviewFailed, &events_, &AccountEvents::overviewFailed);
    connect(worker_, &SessionWorker::mailboxUsage, &events_, &AccountEvents::mailboxUsage);
    connect(worker_, &SessionWorker::deviceState, &events_, &AccountEvents::deviceState);
    connect(worker_, &SessionWorker::contactDevices, &events_, &AccountEvents::contactDevices);
    connect(worker_, &SessionWorker::accountClosed, &events_, &AccountEvents::accountClosed);
    connect(worker_, &SessionWorker::accountLeft, &events_, &AccountEvents::accountLeft);
    connect(worker_, &SessionWorker::facadeInfo, &events_, &AccountEvents::facadeInfo);
    connect(worker_, &SessionWorker::actionOk, &events_, &AccountEvents::actionOk);
    connect(worker_, &SessionWorker::botActionDone, &log_,
        [this](const QString& id, const bool ok, const QString& error) {
            log_.finish(id, ok, ok ? tr("Sent") : error);
            const auto pressed = keyPresses_.constFind(id);
            if (pressed != keyPresses_.cend()) {
                emit events_.botKeySettled(pressed.value(), ok);
                keyPresses_.erase(pressed);
            }
        });
    connect(worker_, &SessionWorker::actionFailed, &events_, &AccountEvents::actionFailed);
    connect(worker_, &SessionWorker::inviteState, &events_, &AccountEvents::inviteState);
    connect(worker_, &SessionWorker::loginSigned, &events_, &AccountEvents::loginSigned);
    connect(worker_, &SessionWorker::connectionLogReady, &events_,
        &AccountEvents::connectionLogReady);
    connect(worker_, &SessionWorker::connectionLogLine, &events_,
        &AccountEvents::connectionLogLine);
    connect(worker_, &SessionWorker::i2pKeyState, &events_, &AccountEvents::i2pKeyState);
    connect(worker_, &SessionWorker::destChangeSettled, &events_,
        &AccountEvents::destChangeSettled);
    connect(worker_, &SessionWorker::callStateChanged, this, &CoreAccount::onCallStateChanged);
    connect(worker_, &SessionWorker::callLevels, &events_, &AccountEvents::callLevels);
    connect(worker_, &SessionWorker::callLogged, this, &CoreAccount::onCallLogged);
    connect(worker_, &SessionWorker::callEndFailed, this, &CoreAccount::onCallEndFailed);
    connect(worker_, &SessionWorker::commandFinished, this, &CoreAccount::onCommandFinished);
    connect(worker_, &SessionWorker::loginSignerReady, this,
        [this](std::shared_ptr<bazarish::client::LoginSigner> signer) {
            loginSigner_ = std::move(signer);
    connect(worker_, &SessionWorker::loginSignerRefused, this, [this]() { loginRefused_ = true; });
        });
    connect(worker_, &SessionWorker::opened, this,
        [this](const QString& fingerprint, const QString& displayName, const bool connected) {
            contacts_.setOwnFingerprint(fingerprint);
            displayName_ = displayName;
            online_ = connected && startOnline_;
            announcePresence();
            setPresenceAway(presenceAway_);
            setPresenceFocused(presenceFocused_);
            watchUnsentRows();
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
            setPresenceFocused(presenceFocused_);
        });
    connect(worker_, &SessionWorker::syncReachable, this, &CoreAccount::onSyncReachable);
    connect(worker_, &SessionWorker::syncLinkStage, this, [this](const client::LinkStage stage) {
        const int before = attemptStatus();
        attemptStage_ = stage;
        if (attemptStatus() != before) {
            announcePresence();
        }
    });
    connect(worker_, &SessionWorker::facadeInfo, this,
        [this](const QString& activeAddress, const QStringList& configured) {
            activeFacade_ = activeAddress;
            hasServer_ = !configured.isEmpty();
            serverKnown_ = true;
            emit changed();
        });
    connect(worker_, &SessionWorker::linksRebuilding, this, [this]() {
        linksRebuilding_ = true;
        reachable_ = false;
        gatewayRefusal_.clear();
        linkFailure_ = client::LinkFailure::eNone;
        attemptStage_.reset();
        announcePresence();
    });
    connect(worker_, &SessionWorker::accountSettings, this,
        [this](const bool, const bool sendReceipts, const bool) { sendReceipts_ = sendReceipts; });
    connect(worker_, &SessionWorker::contactsRefreshed, this,
        [this](const QVector<ContactState>& contacts, const QStringList& blocked) {
            contacts_.reset(contacts, blocked);
            syncAgreeingRows();
            presenceOrderTimer_.start(kPresenceOrderSettleMs);
        });
    presenceOrderTimer_.setSingleShot(true);
    connect(&presenceOrderTimer_, &QTimer::timeout, this, [this]() {
        QStringList order = contacts_.fingerprints();
        std::stable_sort(order.begin(), order.end(), [this](const QString& a, const QString& b) {
            const bool pinnedA = store_.isPinned(a);
            const bool pinnedB = store_.isPinned(b);
            if (pinnedA != pinnedB) {
                return pinnedA;
            }
            return store_.lastTime(a) > store_.lastTime(b);
        });
        QMetaObject::invokeMethod(
            worker_, [worker = worker_, order]() { worker->setPresenceOrder(order); },
            Qt::QueuedConnection);
    });
    connect(&ingest_, &MessageIngest::chatChanged, this,
        [this]() { presenceOrderTimer_.start(kPresenceOrderSettleMs); });
    connect(&ingest_, &MessageIngest::chatsChanged, this,
        [this]() { presenceOrderTimer_.start(kPresenceOrderSettleMs); });
    connect(&ingest_, &MessageIngest::rowAdded, this,
        [this]() { presenceOrderTimer_.start(kPresenceOrderSettleMs); });
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
    connect(&ingest_, &MessageIngest::deliveredThrough, &events_,
        &AccountEvents::deliveredThrough);
    connect(&ingest_, &MessageIngest::reactionsChanged, &events_,
        &AccountEvents::reactionsChanged);
    connect(&ingest_, &MessageIngest::groupChanged, &events_, &AccountEvents::groupChanged);
    connect(&ingest_, &MessageIngest::memberListChanged, &events_,
        &AccountEvents::memberListChanged);
    connect(&ingest_, &MessageIngest::memberListWanted, &commands_,
        &AccountCommands::requestAskGroupMembers);
    connect(&ingest_, &MessageIngest::accountRenamed, &events_, &AccountEvents::accountRenamed);
    connect(&ingest_, &MessageIngest::contactsRevised, &events_,
        &AccountEvents::contactsRevised);
    connect(&ingest_, &MessageIngest::settingsAsked, &commands_,
        &AccountCommands::requestEmitSettings);
    connect(&ingest_, &MessageIngest::readElsewhere, this,
        [this](const QString& peer, const qint64 throughMs) {
            lastReadAckedId_[peer]
                = qMax(lastReadAckedId_.value(peer, 0), store_.lastReadId(peer));
            readElsewhereAtMs_[peer] = qMax(readElsewhereAtMs_.value(peer, 0), throughMs);
            emit events_.chatChanged(peer);
            if (store_.unreadCount(peer) == 0) {
                emit chatAllRead(peer);
            }
        });
    connect(&ingest_, &MessageIngest::editArrived, this,
        [this](const QString& peer, const qint64 localId, const QString& ref) {
            if (peer == viewedPeer_ && receiptsGoTo(peer) && !catchingUp_
                && localId <= lastReadAckedId_.value(peer, 0)) {
                emit commands_.requestSendReceipt(peer, ref);
            }
        });
    connect(&ingest_, &MessageIngest::messageArrived, this,
        [this](const QString& peer, const qint64 id) {
            const StoredMessage arrived = store_.messageById(id);
            if (contacts_.state(peer).notifications || arrived.mentionsMe) {
                emit messageNotification(peer, contacts_.peerName(peer),
                    chatPreviewOf({arrived.text, arrived.type, arrived.attName, arrived.author}));
            }
        });
    connect(&ingest_, &MessageIngest::reactionArrived, this,
        [this](const QString& peer, const QString& target, const QString& emoji) {
            noteReactionToFlash(peer, target);
            tellOfReaction(peer, target, emoji);
        });
    connect(&ingest_, &MessageIngest::groupReactionArrived, this, &CoreAccount::tellOfReaction);

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
    connect(&commands_, &AccountCommands::rememberEmoji, this, &CoreAccount::rememberEmoji);
    connect(&commands_, &AccountCommands::pinChat, this, &CoreAccount::pinChat);
    connect(&commands_, &AccountCommands::clearChat, this, &CoreAccount::clearChat);
    connect(&commands_, &AccountCommands::setChatTimer, this, &CoreAccount::setChatTimer);
    connect(&commands_, &AccountCommands::forgetChat, this, &CoreAccount::forgetChat);
    connect(&commands_, &AccountCommands::clearSavedEverywhere, this,
        &CoreAccount::clearSavedEverywhere);
    connect(&commands_, &AccountCommands::clearSavedHere, this, &CoreAccount::clearSavedHere);
    connect(&commands_, &AccountCommands::addByInvite, this, &CoreAccount::addByInvite);
    connect(&commands_, &AccountCommands::addByAlias, this, &CoreAccount::addByAlias);
    connect(&commands_, &AccountCommands::retryContactAdd, this, &CoreAccount::retryContactAdd);
    connect(&commands_, &AccountCommands::retryContactRequest, this,
        &CoreAccount::retryContactRequest);
    connect(&commands_, &AccountCommands::acceptCall, this, &CoreAccount::acceptCall);
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
            tr("Asking the registry over I2P..."));
    });
    connect(&commands_, &AccountCommands::requestExport, this, [this]() {
        log_.begin(QStringLiteral("export"), QStringLiteral("account"),
            tr("Exporting your backup"), tr("Waiting for this account..."));
    });
    connect(&commands_, &AccountCommands::requestGeneratePersonalKey, this, [this]() {
        log_.begin(QStringLiteral("dest-key"), QStringLiteral("dest"),
            tr("Creating your destination key"), tr("Queued..."));
    });
    connect(worker_, &SessionWorker::connectProgress, this, &CoreAccount::onConnectProgress);
    connect(worker_, &SessionWorker::syncProgress, this, &CoreAccount::onSyncProgress);
    connect(worker_, &SessionWorker::connectionChanged, this, &CoreAccount::settleConnect);
    connect(worker_, &SessionWorker::actionFailed, this,
        [this](const QString& reason) { settleConnect(false, reason); });
    connect(worker_, &SessionWorker::aliasActivationDone, this,
        [this](const bool ok, const QString& text) { log_.finish(kAliasOperationId, ok, text); });
    connect(worker_, &SessionWorker::transferStage, this, &CoreAccount::onTransferStage);
    connect(worker_, &SessionWorker::servedProgress, this, &CoreAccount::onServedProgress);
    connect(worker_, &SessionWorker::servedFinished, this, &CoreAccount::onServedFinished);
    commandTimer_.setInterval(kCommandTickMs);
    connect(&commandTimer_, &QTimer::timeout, this, &CoreAccount::showSlowCommands);

    readSyncTimer_.setSingleShot(true);
    connect(&readSyncTimer_, &QTimer::timeout, this, &CoreAccount::flushReadSync);
    expiryTimer_.setSingleShot(true);
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

void CoreAccount::goOnline()
{
    startOnline_ = true;
    online_ = true;
    announcePresence();
    emit commands_.requestSetSync(true);
    setPresenceFocused(presenceFocused_);
}

void CoreAccount::goOffline()
{
    online_ = false;
    reachable_ = false;
    linksRebuilding_ = false;
    gatewayRefusal_.clear();
    linkFailure_ = client::LinkFailure::eNone;
    attemptStage_.reset();
    announcePresence();
    emit commands_.requestSetSync(false);
    setPresenceFocused(presenceFocused_);
}

void CoreAccount::replayOperations()
{
    for (const OperationRow& row : log_.rows()) {
        emit events_.opChanged(row);
    }
}

void CoreAccount::setPresenceFocused(const bool focused)
{
    presenceFocused_ = focused;
    const bool shown = presenceFocused_ && online_;
    QMetaObject::invokeMethod(
        worker_, [worker = worker_, shown]() { worker->setPresenceFocused(shown); },
        Qt::QueuedConnection);
}

void CoreAccount::setPresenceAway(const bool away)
{
    presenceAway_ = away;
    QMetaObject::invokeMethod(
        worker_, [worker = worker_, away]() { worker->setPresenceAway(away); },
        Qt::QueuedConnection);
}

void CoreAccount::leavePresenceNow()
{
    if (thread_.isRunning()) {
        worker_->leavePresenceNow();
    }
}

void CoreAccount::rebuildI2pLinks()
{
    emit commands_.requestRebuildI2p();
}

void CoreAccount::onSyncReachable(
    const bool ok, const QString& gatewayRefusal, const client::LinkFailure failure)
{
    const QString refusal = ok ? QString() : gatewayRefusal;
    const client::LinkFailure cause = ok ? client::LinkFailure::eNone : failure;
    if (reachable_ == ok && gatewayRefusal_ == refusal && linkFailure_ == cause
        && !linksRebuilding_) {
        return;
    }
    linksRebuilding_ = false;
    reachable_ = ok;
    gatewayRefusal_ = refusal;
    linkFailure_ = cause;
    if (ok) {
        attemptStage_.reset();
    }
    announcePresence();
}

void CoreAccount::transportChanged()
{
    const i2p::TransportState now = i2p::transportState();
    if (now == transport_) {
        return;
    }
    transport_ = now;
    if (now == i2p::TransportState::eUp && linkFailure_ == client::LinkFailure::eNoNetwork) {
        linkFailure_ = client::LinkFailure::eNone;
    }
    announcePresence();
}

int CoreAccount::attemptStatus() const
{
    if (reachable_ || !attemptStage_.has_value()) {
        return static_cast<int>(client::LinkFailure::eNone);
    }
    switch (*attemptStage_) {
    case client::LinkStage::eRouter:
        return static_cast<int>(client::LinkFailure::eNone);
    case client::LinkStage::eLink:
    case client::LinkStage::eTunnels:
        return static_cast<int>(client::LinkFailure::eTunnelsBuilding);
    case client::LinkStage::eDial:
    case client::LinkStage::eExchange:
        return kLinkReachingServer;
    }
    throw std::logic_error("no status for this link stage");
}

client::LinkFailure CoreAccount::shownFailure() const
{
    switch (transport_) {
    case i2p::TransportState::eNoNetwork:
        return client::LinkFailure::eNoNetwork;
    case i2p::TransportState::eGatewayUnreachable:
        return client::LinkFailure::eGatewayUnreachable;
    case i2p::TransportState::eUp:
        break;
    }
    return reachable_ ? client::LinkFailure::eNone : linkFailure_;
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
    const client::LinkFailure failure = shownFailure();
    const int status = linksRebuilding_ && transport_ == i2p::TransportState::eUp
        ? kLinkRebuilding
        : failure == client::LinkFailure::eNone ? attemptStatus()
        : static_cast<int>(failure);
    emit events_.presence(online_, reachable(), gatewayRefusal_, status);
    emit changed();
}

void CoreAccount::open(const QString& file, const QString& passphrase, const bool startOnline)
{
    accountPath_ = file;
    passphrase_ = passphrase;
    startOnline_ = startOnline;
    std::optional<std::vector<client::UnsentRow>> unsent;
    if (!store_.open(file, passphrase)
        || !(unsent = store_.core().unsentOutgoing(
                 DeliveryStatus::Preparing, DeliveryStatus::Delivering))
        || !store_.settleUnfinishedNotes(QStringLiteral("system"), DeliveryStatus::Preparing,
            DeliveryStatus::Received,
            encodeSystemNote(QT_TR_NOOP("The contact request did not finish")))) {
        emit events_.openFailed(tr("This profile could not be opened"));
        return;
    }
    const std::optional<std::string> device = client::storedDeviceId(accountDb());
    if (!device.has_value()) {
        emit events_.openFailed(tr("This profile could not be opened"));
        return;
    }
    unsentAtOpen_ = *unsent;
    ownDevice_ = QString::fromStdString(*device);
    unread_ = store_.unreadTotal();
    store_.core().onExpiryStamped([this](const std::int64_t atMs) { armExpiry(atMs); });
    sweepExpired();
    const SecretBytes key = client::accountkey::keyFor(toPath(file), passphrase.toStdString());
    emit events_.transcriptOpened(file, toQByteArray(key.bytes()));
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
    const QJsonDocument emoji = QJsonDocument::fromJson(
        QByteArray::fromStdString(accountDb().text("recent-emoji")));
    for (const QJsonValue& entry : emoji.array()) {
        recentEmoji_ << entry.toString();
    }
    emit events_.recentEmoji(recentEmoji_);
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
    readSyncTimer_.stop();
    presenceOrderTimer_.stop();
    commandTimer_.stop();
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

void CoreAccount::onMessageReceived(const bazarish::client::IncomingMessage& message)
{
    if (!ingest_.takeIn(message)) {
        emit events_.actionFailed(tr("Receiving stopped: %1").arg(store_.lastError()));
        return;
    }
    if (!message.pendingId.empty()) {
        emit commands_.requestAckPending(QString::fromStdString(message.pendingId));
    }
}

// A group host ignores receipts.
bool CoreAccount::receiptsGoTo(const QString& peer) const
{
    return sendReceipts_ && !store_.groupState(peer).has_value();
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
    if (catchingUp_) {
        heldReceipts_[peer] = {sentAtMs, e2eId};
        return;
    }
    readSyncTimer_.start(kReadSyncIdleMs);
    if (!receiptsGoTo(peer) || !online_) {
        return;
    }
    emit commands_.requestSendReceipt(peer, e2eId);
}

void CoreAccount::markChatRead(const QString& peer)
{
    const QVector<StoredMessage> latest = store_.latestMessages(peer, kReadScanRows);
    for (auto it = latest.crbegin(); it != latest.crend(); ++it) {
        if (!it->outgoing && !it->e2eId.isEmpty() && it->type != QLatin1String("system")) {
            markRead(peer, it->id, it->e2eId, it->ts);
            break;
        }
    }
    emit chatAllRead(peer);
}

void CoreAccount::onSyncProgress(
    const bool syncing, const bool shown, const qint64 takenBytes, const qint64 totalBytes)
{
    emit events_.syncProgress(syncing, shown, takenBytes, totalBytes);
    if (syncing == catchingUp_) {
        return;
    }
    catchingUp_ = syncing;
    if (!catchingUp_) {
        releaseHeldReads();
    }
}

void CoreAccount::releaseHeldReads()
{
    for (auto it = pendingReadSync_.begin(); it != pendingReadSync_.end();) {
        it = it.value() <= readElsewhereAtMs_.value(it.key(), 0) ? pendingReadSync_.erase(it)
                                                                 : std::next(it);
    }
    if (sendReceipts_ && online_) {
        for (auto it = heldReceipts_.constBegin(); it != heldReceipts_.constEnd(); ++it) {
            if (it.value().sentAtMs > readElsewhereAtMs_.value(it.key(), 0)
                && receiptsGoTo(it.key())) {
                emit commands_.requestSendReceipt(it.key(), it.value().e2eId);
            }
        }
    }
    heldReceipts_.clear();
    readElsewhereAtMs_.clear();
    flushReadSync();
}

void CoreAccount::flushReadSync()
{
    readSyncTimer_.stop();
    if (catchingUp_) {
        return;
    }
    if (!online_) {
        pendingReadSync_.clear();
        return;
    }
    for (auto it = pendingReadSync_.constBegin(); it != pendingReadSync_.constEnd(); ++it) {
        emit commands_.requestSyncRead(it.key(), it.value());
    }
    pendingReadSync_.clear();
}

void CoreAccount::tellOfReaction(const QString& peer, const QString& target, const QString& emoji)
{
    const StoredMessage message = store_.messageByE2e(target, peer);
    if (!message.outgoing || !contacts_.state(peer).notifications) {
        return;
    }
    emit reactionNotification(peer, contacts_.peerName(peer), emoji,
        chatPreviewOf({message.text, message.type, message.attName, message.author}));
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
    worker_->releaseLinksNow();
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

QString CoreAccount::loginUnavailableText() const
{
    return loginRefused_ ? tr("Only the primary device signs in by key")
                         : tr("This account is still opening");
}

void CoreAccount::sendLogin(const QString& challenge)
{
    const qint64 startedAt = nowMillis();
    if (!loginSigner_) {
        emit events_.loginSendState(challenge, eLoginRefused, loginUnavailableText(), startedAt);
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
        : !status                          ? siteFailureText(client::LinkFailure::eServerUnreachable)
        : accepted                         ? tr("Accepted")
                                           : siteFailureText(client::LinkFailure::eServerRefused);
    log_.finish(op, accepted, said);
    emit events_.loginSendState(
        challenge, accepted ? eLoginAccepted : eLoginRefused, said, startedAtMs);
}

void CoreAccount::fetchLogin(const QString& link)
{
    if (!loginSigner_) {
        emit events_.loginFetchState(link, false, loginUnavailableText(), {});
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
        : !challenge                       ? siteFailureText(client::LinkFailure::eServerUnreachable)
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
        const QString status = at == 0 ? tr("Working on it...") : tr("Waiting its turn...");
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
