// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"
#include "SystemNotes.hpp"

#include "I2pRouter.hpp"
#include "Invite.hpp"

#include <QFile>

#include <QJsonDocument>

#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "DeliveryStatus.hpp"
#include "PhotoShot.hpp"
#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QByteArray>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaMethod>
#include <QSet>
#include <QImage>
#include <QMimeDatabase>
#include <QQuickItemGrabResult>
#include <chrono>
#include <QTimer>
#include <cstring>
#include <QUrl>

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

struct ResolvedContactAddQueue {
    std::mutex mutex;
    struct Entry {
        QString opId;
        bazarish::client::Session::ContactCardResolved resolved;
    };
    std::vector<Entry> results;
    std::vector<std::pair<QString, QString>> stages;
};

struct AliasErrandQueue {
    std::mutex mutex;
    std::vector<bazarish::client::Session::AliasErrandResult> results;
};

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

namespace {
constexpr int kSchemeSeparatorLength = 3;

QString facadeHost(const QString& url)
{
    QString rest = url.trimmed();
    const int schemeEnd = rest.indexOf(QStringLiteral("://"));
    if (schemeEnd >= 0) {
        rest = rest.mid(schemeEnd + kSchemeSeparatorLength);
    }
    const int pathStart = rest.indexOf(QLatin1Char('/'));
    if (pathStart >= 0) {
        rest = rest.left(pathStart);
    }
    return rest;
}

constexpr int kCommandTickMs = 200;

// A reconnect this client asked for is not reported before this much of it.
constexpr int kLinkRebuildGraceMs = 1500;

constexpr qint64 kCommandVisibleAfterMs = 400;

const QSet<QByteArray> kSelfDescribingCommands = {
    "requestOpen", "requestConnect", "requestShutdown", "requestSendText", "requestSendFile",
    "requestSendPicture", "requestSendVoice", "requestSendCallback", "requestSendCommand",
    "requestAddByInvite", "requestAddByAlias", "requestAcceptContact", "requestInviteSig",
    "requestExport", "requestSaveAttachment", "requestActivateAliasServicing",
    "requestPublishThisDeviceAddress", "requestPublishFreshAddress", "requestRefreshI2pStatus",
    "requestRefreshStorageUsage", "requestRefreshDevices", "requestForgetDevice",
    "requestCloseAccountOnServer", "requestGeneratePersonalKey", "requestLoadPersonalKey",
    "requestReplacePersonalKey", "requestPublishPersonalDest", "requestSetAliasBinding",
};

const QString kConnectOperationId = QStringLiteral("connect");

constexpr double kPercentFull = 100.0;

bool isRetryPhase(const QString& phase)
{
    return phase.startsWith(QLatin1StringView(bazarish::client::kPhaseRetryPrefix));
}

QString humanDeliveryPhase(const QString& phase)
{
    if (phase == QLatin1StringView(bazarish::client::kPhasePreparing)) {
        return SessionController::tr("Preparing an address to send from…");
    }
    if (phase == QLatin1StringView(bazarish::client::kPhaseDialing)) {
        return SessionController::tr("Reaching the recipient's server…");
    }
    if (phase == QLatin1StringView(bazarish::client::kPhaseSending)) {
        return SessionController::tr("Sending over I2P…");
    }
    if (isRetryPhase(phase)) {
        const QLatin1StringView prefix(bazarish::client::kPhaseRetryPrefix);
        const QStringList parts = phase.sliced(prefix.size()).trimmed().split(QChar('/'));
        if (parts.size() == 2) {
            return SessionController::tr("Trying again (%1 of %2)…")
                .arg(parts.at(0), parts.at(1));
        }
    }
    return phase;
}

constexpr int kPageSize = 50;

constexpr int kBusyPaintDelayMs = 32;

constexpr int kReadSyncIdleMs = 4000;

}  // namespace

// Both travel through a queued signal from the worker, so Qt has to know them by name.
namespace {
const int kLoginSignerMetaType
    = qRegisterMetaType<std::shared_ptr<bazarish::client::LoginSigner>>(
        "std::shared_ptr<bazarish::client::LoginSigner>");
const int kContactStateMetaType
    = qRegisterMetaType<QVector<ContactState>>("QVector<ContactState>");
}  // namespace

SessionController::SessionController(QObject* parent)
    : QObject(parent)
{
    contactsProxy_.setSourceModel(&contacts_);
    contactsProxy_.setFilterRole(ContactListModel::NameRole);
    contactsProxy_.setFilterCaseSensitivity(Qt::CaseInsensitive);

    readSyncTimer_.setSingleShot(true);
    connect(&readSyncTimer_, &QTimer::timeout, this, &SessionController::flushReadSync);

    worker_ = new SessionWorker();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(this, &SessionController::requestOpen, worker_, &SessionWorker::openAccount);
    connect(this, &SessionController::requestConnect, worker_, &SessionWorker::connectAndRegister);
    connect(worker_, &SessionWorker::connectProgress, this, &SessionController::onConnectProgress);
    connect(this, &SessionController::requestSendText, worker_, &SessionWorker::sendText);
    connect(this, &SessionController::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(this, &SessionController::requestSendPicture, worker_, &SessionWorker::sendPicture);
    connect(this, &SessionController::requestSendVoice, worker_, &SessionWorker::sendVoice);
    connect(this, &SessionController::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(this, &SessionController::requestSendReaction, worker_, &SessionWorker::sendReaction);
    connect(this, &SessionController::requestSendCallback, worker_, &SessionWorker::sendCallback);
    connect(this, &SessionController::requestSendCommand, worker_, &SessionWorker::sendCommand);
    connect(this, &SessionController::requestSendEdit, worker_, &SessionWorker::sendEdit);
    connect(this, &SessionController::requestSendDelete, worker_, &SessionWorker::sendDelete);
    connect(this, &SessionController::requestUnsend, worker_, &SessionWorker::dropSentFile);
    connect(this, &SessionController::requestSetAvatar, worker_, &SessionWorker::setAvatar);
    connect(this, &SessionController::requestClearAvatar, worker_, &SessionWorker::clearAvatar);
    connect(this, &SessionController::requestSetDisplayName, worker_,
        &SessionWorker::setDisplayName);
    connect(this, &SessionController::requestRenameContact, worker_, &SessionWorker::renameContact);
    connect(this, &SessionController::requestRemoveContact, worker_, &SessionWorker::removeContact);
    connect(this, &SessionController::requestClearSaved, worker_, &SessionWorker::clearSaved);
    connect(this, &SessionController::requestSetBlocked, worker_, &SessionWorker::setBlocked);
    connect(this, &SessionController::requestSetContactNotifications, worker_,
        &SessionWorker::setContactNotifications);
    connect(this, &SessionController::requestSetContactCalls, worker_,
        &SessionWorker::setContactCalls);
    connect(this, &SessionController::requestSyncChatPin, worker_, &SessionWorker::syncChatPin);
    connect(this, &SessionController::requestSyncRead, worker_, &SessionWorker::syncRead);
    connect(this, &SessionController::requestSyncChatClear, worker_,
        &SessionWorker::syncChatClear);
    connect(this, &SessionController::requestEmitSettings, worker_,
        &SessionWorker::emitSettings);
    connect(this, &SessionController::requestClearChatForEveryone, worker_,
        &SessionWorker::clearChatForEveryone);
    connect(this, &SessionController::requestAddByInvite, worker_, &SessionWorker::addByInvite);
    connect(this, &SessionController::requestAddByAlias, worker_, &SessionWorker::addByAlias);
    connect(this, &SessionController::requestAcceptContact, worker_, &SessionWorker::acceptContact);
    connect(this, &SessionController::requestInviteSig, worker_, &SessionWorker::requestInvite);
    connect(this, &SessionController::requestSignLoginSig, worker_, &SessionWorker::signLogin);
    connect(this, &SessionController::requestPublishThisDeviceAddress, worker_,
        &SessionWorker::publishThisDeviceAddress);
    connect(this, &SessionController::requestPublishFreshAddress, worker_,
        &SessionWorker::publishFreshAddress);
    connect(worker_, &SessionWorker::addressMismatch, this,
        [this](const QString& served, const QString& ours) {
            emit addressNeedsChoice(served, ours);
        });
    connect(worker_, &SessionWorker::loginSignerReady, this,
        [this](std::shared_ptr<bazarish::client::LoginSigner> signer) {
            loginSigner_ = std::move(signer);
        });
    connect(this, &SessionController::requestConnectionLog, worker_,
        &SessionWorker::connectionLog);
    connect(this, &SessionController::requestContactsFromDevices, worker_,
        &SessionWorker::askDevicesForContacts);
    connect(this, &SessionController::requestShutdown, worker_, &SessionWorker::shutdown);
    connect(worker_, &SessionWorker::stopped, this, [this]() {
        accountDb_.reset();
        store_.close();
        thread_.quit();
    });
    connect(&thread_, &QThread::finished, this, [this]() { emit closed(); });
    connect(this, &SessionController::requestClearConnectionLog, worker_,
        &SessionWorker::clearConnectionLog);
    connect(this, &SessionController::requestSaveAttachment, worker_,
        &SessionWorker::saveAttachment);
    connect(worker_, &SessionWorker::downloadProgress, this,
        &SessionController::onDownloadProgress);
    connect(worker_, &SessionWorker::servedProgress, this, &SessionController::onServedProgress);
    connect(worker_, &SessionWorker::transferStage, this, &SessionController::onTransferStage);
    connect(worker_, &SessionWorker::servedFinished, this, &SessionController::onServedFinished);
    connect(worker_, &SessionWorker::downloadFinished, this,
        &SessionController::onDownloadFinished);
    connect(this, &SessionController::requestExport, worker_, &SessionWorker::exportAccount);
    connect(this, &SessionController::requestStartPairing, worker_,
        &SessionWorker::startPairing);
    connect(this, &SessionController::requestStopPairing, worker_, &SessionWorker::stopPairing);
    connect(worker_, &SessionWorker::pairOfferReady, this, &SessionController::onPairOfferReady);
    connect(worker_, &SessionWorker::pairStage, this, &SessionController::onPairStage);
    connect(worker_, &SessionWorker::pairFinished, this, &SessionController::onPairFinished);
    connect(this, &SessionController::requestChangePassphrase, worker_,
        &SessionWorker::changePassphrase);
    connect(this, &SessionController::requestActivateAliasServicing, worker_,
        &SessionWorker::activateAliasServicing);
    connect(worker_, &SessionWorker::aliasHoldings, this,
        [this](const QVariantList& rows, const QString& note) {
            aliasHoldings_ = rows;
            aliasNote_ = note;
            emit aliasChanged();
        });
    connect(worker_, &SessionWorker::aliasActivationDone, this, [this](const bool ok,
                                                                   const QString& text) {
        aliasBusy_ = false;
        emit aliasChanged();
        finishOperation(kAliasOperationId, ok, text);
        if (ok) {
            emit actionOk(text);
        } else {
            emit actionFailed(text);
        }
    });
    connect(this, &SessionController::requestSetSync, worker_, &SessionWorker::setSyncEnabled);
    connect(this, &SessionController::requestRebuildI2p, worker_, &SessionWorker::rebuildI2pLinks);
    connect(this, &SessionController::requestCancelTransfer, worker_,
        &SessionWorker::cancelTransfer);
    connect(this, &SessionController::requestGeneratePersonalKey, worker_,
        &SessionWorker::generatePersonalKey);
    connect(this, &SessionController::requestLoadPersonalKey, worker_,
        &SessionWorker::loadPersonalKey);
    connect(this, &SessionController::requestReplacePersonalKey, worker_,
        &SessionWorker::replacePersonalKey);
    connect(this, &SessionController::requestSetAliasBinding, worker_,
        &SessionWorker::setAliasBinding);
    connect(this, &SessionController::requestRetryRoutingTo, worker_,
        &SessionWorker::retryRoutingTo);
    connect(this, &SessionController::requestPublishPersonalDest, worker_,
        &SessionWorker::publishPersonalDest);
    connect(this, &SessionController::requestSetDelegationDays, worker_,
        &SessionWorker::setDelegationDays);
    connect(this, &SessionController::requestSetAcceptCalls, worker_,
        &SessionWorker::setAcceptCalls);
    connect(this, &SessionController::requestSetSendReceipts, worker_,
        &SessionWorker::setSendReceipts);
    connect(this, &SessionController::requestDisablePersonalDest, worker_,
        &SessionWorker::disablePersonalDest);
    connect(this, &SessionController::requestRefreshI2pStatus, worker_,
        &SessionWorker::refreshI2pStatus);
    connect(this, &SessionController::requestRefreshStorageUsage, worker_,
        &SessionWorker::refreshStorageUsage);
    connect(this, &SessionController::requestStartCall, worker_, &SessionWorker::startCall);
    connect(this, &SessionController::requestAcceptCall, worker_, &SessionWorker::acceptCall);
    connect(this, &SessionController::requestDeclineCall, worker_, &SessionWorker::declineCall);
    connect(this, &SessionController::requestEndCall, worker_, &SessionWorker::endCall);
    connect(this, &SessionController::requestSetCallMuted, worker_, &SessionWorker::setCallMuted);

    connect(worker_, &SessionWorker::opened, this, &SessionController::onOpened);
    connect(worker_, &SessionWorker::accountSettings, this,
        [this](const bool acceptCalls, const bool sendReceipts) {
            if (sendReceipts_ != sendReceipts) {
                sendReceipts_ = sendReceipts;
                emit sendReceiptsChanged();
            }
            if (acceptCalls_ != acceptCalls) {
                acceptCalls_ = acceptCalls;
                emit acceptCallsChanged();
            }
        });
    connect(worker_, &SessionWorker::renamed, this, [this](const QString& newName) {
        if (newName != displayName_) {
            displayName_ = newName;
            emit identityChanged();
        }
    });
    connect(worker_, &SessionWorker::openFailed, this, &SessionController::openFailed);
    connect(worker_, &SessionWorker::connectionChanged, this,
        &SessionController::onConnectionChanged);
    connect(worker_, &SessionWorker::messageReceived, this,
        &SessionController::onMessageReceived);
    connect(worker_, &SessionWorker::messageReceived, this,
        &SessionController::ackAfterReceive);
    connect(this, &SessionController::requestAckPending, worker_, &SessionWorker::ackPending);
    connect(worker_, &SessionWorker::contactsRefreshed, this,
        [this](const QVector<ContactState>& contacts, const QStringList& blocked) {
            contactFps_.clear();
            contactState_.clear();
            for (const ContactState& contact : contacts) {
                contactFps_ << contact.fingerprint;
                contactState_.insert(contact.fingerprint, contact);
            }
            blocked_ = blocked;
            syncAgreeingRows();
            rebuildChatList();
            emit activePeerNameChanged();
            ++contactsRevision_;
            emit contactsRevisionChanged();
        });
    connect(worker_, &SessionWorker::avatarReady, this, &SessionController::onAvatarReady);
    connect(worker_, &SessionWorker::sendProgress, this, &SessionController::onSendProgress);
    connect(worker_, &SessionWorker::uploadProgress, this, &SessionController::onUploadProgress);
    connect(worker_, &SessionWorker::sendResult, this, &SessionController::onSendResult);
    connect(worker_, &SessionWorker::sendPhase, this, &SessionController::onSendPhase);
    connect(worker_, &SessionWorker::contactRequestSent, this,
        &SessionController::onContactRequestSent);
    connect(worker_, &SessionWorker::contactAddStage, this,
        &SessionController::onContactAddStage);
    connect(this, &SessionController::requestForgetPendingAdd, worker_,
        &SessionWorker::forgetPendingAdd);
    connect(worker_, &SessionWorker::contactAddDone, this, &SessionController::onContactAddDone);
    connect(worker_, &SessionWorker::routingTold, this, &SessionController::onRoutingTold);
    connect(worker_, &SessionWorker::contactAlreadyKnown, this,
        &SessionController::onContactAlreadyKnown);
    connect(worker_, &SessionWorker::contactAddRateLimited, this,
        &SessionController::onContactAddRateLimited);
    connect(worker_, &SessionWorker::contactAccepted, this,
        &SessionController::onContactAccepted);
    connect(worker_, &SessionWorker::opBegin, this, &SessionController::onOpBegin);
    connect(worker_, &SessionWorker::opDone, this, &SessionController::onOpDone);
    connect(worker_, &SessionWorker::opProgress, this,
        [this](const QString& opId, const QString& status) { updateOperation(opId, status); });
    connect(worker_, &SessionWorker::syncReachable, this, &SessionController::onSyncReachable);
    connect(worker_, &SessionWorker::approvalState, this, &SessionController::onApprovalState);
    connect(worker_, &SessionWorker::devicesReady, this, &SessionController::onDevicesReady);
    connect(this, &SessionController::requestRefreshDevices, worker_,
        &SessionWorker::refreshDevices);
    connect(this, &SessionController::requestForgetDevice, worker_, &SessionWorker::forgetDevice);
    connect(this, &SessionController::requestCloseAccountOnServer, worker_,
        &SessionWorker::closeAccountOnServer);
    connect(worker_, &SessionWorker::accountClosed, this,
        &SessionController::accountClosedOnServer);
    connect(worker_, &SessionWorker::facadeInfo, this, &SessionController::onFacadeInfo);
    connect(worker_, &SessionWorker::actionOk, this, &SessionController::actionOk);
    connect(worker_, &SessionWorker::botActionDone, this,
        [this](const QString& opId, const bool ok, const QString& error) {
            finishOperation(opId, ok, ok ? tr("Sent") : error);
        });
    connect(worker_, &SessionWorker::actionFailed, this, [this](const QString& reason) {
        setAvatarBusy(false);
        if (connecting_) {
            connecting_ = false;
            connectPhase_.clear();
            connectError_ = reason;
            finishOperation(kConnectOperationId, false, reason);
            emit connectStateChanged();
        }
        emit actionFailed(reason);
    });
    connect(worker_, &SessionWorker::inviteReady, this, [this](const QString& uri) {
        if (ownInvite_ != uri) {
            ownInvite_ = uri;
            emit ownInviteChanged();
        }
        emit inviteReady(uri);
    });
    connect(worker_, &SessionWorker::inviteUnavailable, this,
        &SessionController::inviteUnavailable);
    connect(worker_, &SessionWorker::loginSigned, this, &SessionController::loginSigned);
    connect(worker_, &SessionWorker::connectionLogReady, this,
        &SessionController::connectionLogUpdated);
    connect(worker_, &SessionWorker::serverHello, this, &SessionController::serverHello);
    connect(worker_, &SessionWorker::i2pStatus, this, &SessionController::onI2pStatus);
    connect(worker_, &SessionWorker::i2pKeyState, this, &SessionController::onI2pKeyState);
    connect(worker_, &SessionWorker::i2pServedAddress, this, [this](const QString& address) {
        if (i2pServedAddress_ == address) {
            return;
        }
        i2pServedAddress_ = address;
        emit i2pStatusChanged();
    });
    connect(worker_, &SessionWorker::storageUsageReady, this,
        &SessionController::onStorageUsageReady);
    connect(worker_, &SessionWorker::callStateChanged, this,
        &SessionController::onCallStateChanged);
    connect(worker_, &SessionWorker::callLogged, this, &SessionController::onCallLogged);

    connect(&contacts_, &QAbstractItemModel::dataChanged, this,
        &SessionController::refreshUnreadTotal);
    connect(&contacts_, &QAbstractItemModel::rowsInserted, this,
        &SessionController::refreshUnreadTotal);
    connect(&contacts_, &QAbstractItemModel::modelReset, this,
        &SessionController::refreshUnreadTotal);

    const QMetaObject* const meta = metaObject();
    const QMetaMethod noted = meta->method(meta->indexOfSlot("noteCommandQueued()"));
    const QMetaMethod done = SessionWorker::staticMetaObject.method(
        SessionWorker::staticMetaObject.indexOfSlot("noteCommandDone()"));
    QSet<QByteArray> bracketed;
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() != QMetaMethod::Signal
            || !method.name().startsWith("request")
            || kSelfDescribingCommands.contains(method.name())
            || bracketed.contains(method.name())) {
            continue;
        }
        bracketed.insert(method.name());
        connect(this, method, this, noted);
        connect(this, method, worker_, done);
    }
    connect(worker_, &SessionWorker::commandFinished, this,
        &SessionController::onCommandFinished);
    commandTimer_.setInterval(kCommandTickMs);
    connect(&commandTimer_, &QTimer::timeout, this, &SessionController::showSlowCommands);
    rebuildGrace_.setSingleShot(true);
    rebuildGrace_.setInterval(kLinkRebuildGraceMs);
    connect(&rebuildGrace_, &QTimer::timeout, this, [this]() {
        linksRebuilding_ = false;
        onSyncReachable(false);
    });

    thread_.start();
}

namespace {

QString commandTitle(const QByteArray& signalName)
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
        {"requestContactsFromDevices", QT_TR_NOOP("Asking your other devices")},
        {"requestSetSync", QT_TR_NOOP("Going online")},
        {"requestRebuildI2p", QT_TR_NOOP("Rebuilding the I2P destinations")},
        {"requestInviteSig", QT_TR_NOOP("Preparing your invite")},
        {"requestSignLoginSig", QT_TR_NOOP("Signing in")},
    };
    if (const auto found = kNamed.constFind(signalName); found != kNamed.cend()) {
        return SessionController::tr(found.value());
    }
    QString words;
    for (int at = static_cast<int>(strlen("request")); at < signalName.size(); ++at) {
        const char letter = signalName.at(at);
        if (letter >= 'A' && letter <= 'Z' && !words.isEmpty()) {
            words += QLatin1Char(' ');
            words += QLatin1Char(letter - 'A' + 'a');
            continue;
        }
        words += QLatin1Char(letter);
    }
    return words;
}

}  // namespace

void SessionController::noteCommandQueued()
{
    const QMetaMethod signal = metaObject()->method(senderSignalIndex());
    QueuedCommand command;
    command.id = QStringLiteral("cmd:") + QString::number(++commandSeq_);
    command.title = commandTitle(signal.name());
    command.queuedAtMs = nowMillis();
    commandQueue_.append(command);
    if (!commandTimer_.isActive()) {
        commandTimer_.start();
    }
}

void SessionController::showSlowCommands()
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
            beginOperation(command.id, QStringLiteral("command"), command.title, status);
            continue;
        }
        updateOperation(command.id, status);
    }
    emit operationsChanged();
}

void SessionController::onCommandFinished()
{
    if (commandQueue_.isEmpty()) {
        return;
    }
    const QueuedCommand command = commandQueue_.takeFirst();
    if (command.shown) {
        finishOperation(command.id, true, tr("Done"));
    }
    if (commandQueue_.isEmpty()) {
        commandTimer_.stop();
    }
}

void SessionController::refreshUnreadTotal()
{
    const int total = contacts_.totalUnread();
    if (total != unreadTotal_) {
        unreadTotal_ = total;
        emit unreadTotalChanged();
    }
}

SessionController::~SessionController()
{
    shutdown();
}

void SessionController::beginShutdown()
{
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    if (!thread_.isRunning()) {
        emit closed();
        return;
    }
    emit requestShutdown();
}

void SessionController::shutdown()
{
    shuttingDown_ = true;
    if (thread_.isRunning()) {
        thread_.quit();
        thread_.wait();
    }
    accountDb_.reset();
    store_.close();
}

client::AccountDb& SessionController::accountDb()
{
    if (!accountDb_) {
        accountDb_ = std::make_unique<client::AccountDb>(
            accountPath_.toStdString(), accountPassphrase_.toStdString());
    }
    return *accountDb_;
}

void SessionController::open(const QString& file, const QString& accountId,
    const QString& passphrase, const bool startOnline)
{
    startOnline_ = startOnline;
    accountId_ = accountId;
    accountPath_ = file;
    accountPassphrase_ = passphrase;
    if (!store_.open(accountId, file, passphrase)) {
        emit openFailed(tr("This profile could not be opened."));
        return;
    }
    const QJsonDocument recents = QJsonDocument::fromJson(
        QByteArray::fromStdString(accountDb().text("recent-reactions")));
    for (const QJsonValue& entry : recents.array()) {
        recentReactions_ << entry.toString();
    }
    if (!recentReactions_.isEmpty()) {
        emit recentReactionsChanged();
    }
    const QJsonDocument flashes = QJsonDocument::fromJson(
        QByteArray::fromStdString(accountDb().text("reactions-to-flash")));
    for (const QJsonValue& entry : flashes.array()) {
        reactionsToFlash_ << entry.toString();
    }
    store_.failUnsentOnLoad(
        DeliveryStatus::Preparing, DeliveryStatus::Delivering, DeliveryStatus::Failed);
    store_.settleUnfinishedNotes(QStringLiteral("system"), DeliveryStatus::Preparing,
        DeliveryStatus::Received,
        encodeSystemNote(QT_TR_NOOP("The contact request did not finish.")));
    emit requestOpen(file, passphrase, startOnline);
}

void SessionController::connectServer(const QStringList& facadeUrls, const QString& serverFp,
    const QStringList& reseedUrls)
{
    connecting_ = true;
    connectPercent_ = 0;
    connectPhase_ = tr("Starting…");
    connectError_.clear();
    beginOperation(kConnectOperationId, QStringLiteral("connect"),
        tr("Connecting this account"), connectPhase_);
    emit connectStateChanged();
    emit requestConnect(facadeUrls, serverFp, reseedUrls);
}

void SessionController::onConnectProgress(const int percent, const QString& phase)
{
    if (percent < connectPercent_) {
        return;
    }
    connectPercent_ = percent;
    connectPhase_ = phase;
    updateOperation(kConnectOperationId, phase, {}, percent / kPercentFull);
    emit connectStateChanged();
}

QString SessionController::activeFacadeHost() const
{
    return facadeHost(activeFacade_);
}

void SessionController::onFacadeInfo(const QString& activeUrl, const QStringList& configured,
    const QString& serverFp, const QStringList& reseeds)
{
    activeFacade_ = activeUrl;
    configuredFacades_ = configured;
    serverFp_ = serverFp;
    configuredReseeds_ = reseeds;
    emit facadeInfoChanged();
}

QVariantMap SessionController::parseServerLink(const QString& uri) const
{
    QVariantMap result;
    try {
        const bazarish::client::ServerLink link
            = bazarish::client::decodeServerLink(uri.trimmed().toStdString());
        result["serverFp"] = QString::fromStdString(link.serverFingerprint);
        QStringList facades;
        for (const std::string& url : link.facadeUrls) {
            facades << QString::fromStdString(url);
        }
        result["facades"] = facades;
        QStringList reseeds;
        for (const std::string& url : link.reseedUrls) {
            reseeds << QString::fromStdString(url);
        }
        result["reseeds"] = reseeds;
    } catch (const std::exception& error) {
        bazarish::log::debug("server link not parsed: {}", error.what());
    }
    return result;
}

void SessionController::activateConversation(const QString& peer)
{
    flushReadSync();
    activePeer_ = peer;
    emit activePeerChanged();
    emit activePeerNameChanged();
    lastReadAckedId_[peer] = qMax(lastReadAckedId_.value(peer, 0), store_.lastReadId(peer));
}

void SessionController::loadLatestWindow()
{
    const QVector<StoredMessage> msgs = store_.latestMessages(activePeer_, kPageSize);
    oldestLoadedId_ = msgs.isEmpty() ? 0 : msgs.front().id;
    newestLoadedId_ = msgs.isEmpty() ? 0 : msgs.back().id;
    hasMoreOlder_ = !msgs.isEmpty() && store_.hasMessagesBefore(activePeer_, oldestLoadedId_);
    hasMoreNewer_ = false;
    conversation_.setMessages(msgs);
    requestPicturesFor(msgs);
    replayTransfersForActivePeer();
    emit pagingChanged();
}

void SessionController::showInActiveView(const StoredMessage& m, bool isOwn)
{
    if (m.peer != activePeer_) {
        return;
    }
    if (hasMoreNewer_) {
        if (isOwn) {
            loadLatestWindow();
            emit scrollToBottom();
        }
        return;
    }
    conversation_.appendMessage(m);
    newestLoadedId_ = m.id;
}

void SessionController::closeConversation()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    activePeer_.clear();
    conversation_.setMessages({});
    emit activePeerChanged();
    emit activePeerNameChanged();
}

void SessionController::openConversation(const QString& peer)
{
    activateConversation(peer);
    const qint64 firstUnread = store_.firstUnreadId(peer);
    loadLatestWindow();
    if (firstUnread > 0) {
        if (oldestLoadedId_ > 0 && firstUnread >= oldestLoadedId_) {
            emit scrollToUnread(firstUnread);
        } else {
            openWindowAtUnread(peer, firstUnread);
        }
    }
}

void SessionController::openWindowAtUnread(const QString& peer, qint64 firstUnread)
{
    const QVector<StoredMessage> win = store_.newerMessages(peer, firstUnread - 1, kPageSize);
    if (win.isEmpty()) {
        loadLatestWindow();
        return;
    }
    oldestLoadedId_ = win.front().id;
    newestLoadedId_ = win.back().id;
    hasMoreOlder_ = store_.hasMessagesBefore(peer, oldestLoadedId_);
    hasMoreNewer_ = store_.hasMessagesAfter(peer, newestLoadedId_);
    conversation_.setMessages(win);
    requestPicturesFor(win);
    replayTransfersForActivePeer();
    emit pagingChanged();
    emit scrollToUnread(firstUnread);
}

void SessionController::saveScroll(const QString& peer, int anchorRow, bool stick)
{
    scrollPeer_ = peer;
    scrollAnchorRow_ = anchorRow;
    scrollStick_ = stick;
}

QVariantMap SessionController::scrollFor(const QString& peer) const
{
    QVariantMap m;
    const bool has = !peer.isEmpty() && peer == scrollPeer_;
    m[QStringLiteral("has")] = has;
    m[QStringLiteral("anchor")] = scrollAnchorRow_;
    m[QStringLiteral("stick")] = scrollStick_;
    return m;
}

void SessionController::openConversationAtMessage(const QString& peer, qint64 localId)
{
    activateConversation(peer);
    const QVector<StoredMessage> win = store_.olderMessages(peer, localId + 1, kPageSize);
    oldestLoadedId_ = win.isEmpty() ? 0 : win.front().id;
    newestLoadedId_ = win.isEmpty() ? 0 : win.back().id;
    hasMoreOlder_ = !win.isEmpty() && store_.hasMessagesBefore(peer, oldestLoadedId_);
    hasMoreNewer_ = store_.hasMessagesAfter(peer, newestLoadedId_);
    conversation_.setMessages(win);
    requestPicturesFor(win);
    replayTransfersForActivePeer();
    emit pagingChanged();
    emit scrollToMessage(localId);
}

int SessionController::loadOlderMessages()
{
    if (!hasMoreOlder_ || activePeer_.isEmpty()) {
        return 0;
    }
    const QVector<StoredMessage> older
        = store_.olderMessages(activePeer_, oldestLoadedId_, kPageSize);
    if (older.isEmpty()) {
        hasMoreOlder_ = false;
        emit pagingChanged();
        return 0;
    }
    oldestLoadedId_ = older.front().id;
    hasMoreOlder_ = store_.hasMessagesBefore(activePeer_, oldestLoadedId_);
    conversation_.prependMessages(older);
    requestPicturesFor(older);
    emit pagingChanged();
    return static_cast<int>(older.size());
}

int SessionController::loadNewerMessages()
{
    if (!hasMoreNewer_ || activePeer_.isEmpty()) {
        return 0;
    }
    const QVector<StoredMessage> newer
        = store_.newerMessages(activePeer_, newestLoadedId_, kPageSize);
    if (newer.isEmpty()) {
        hasMoreNewer_ = false;
        emit pagingChanged();
        return 0;
    }
    newestLoadedId_ = newer.back().id;
    hasMoreNewer_ = store_.hasMessagesAfter(activePeer_, newestLoadedId_);
    conversation_.appendMessages(newer);
    requestPicturesFor(newer);
    emit pagingChanged();
    return static_cast<int>(newer.size());
}

void SessionController::jumpToLatest()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    if (hasMoreNewer_) {
        loadLatestWindow();
    }
    emit scrollToBottom();
}

bool SessionController::atNewest() const
{
    return !hasMoreNewer_;
}

bool SessionController::hasMoreOlder() const
{
    return hasMoreOlder_;
}

QVariantList SessionController::searchMessages(const QString& query)
{
    QVariantList results;
    if (activePeer_.isEmpty()) {
        return results;
    }
    for (const SearchHit& hit : store_.searchInPeer(activePeer_, query)) {
        QVariantMap row;
        row["id"] = hit.id;
        row["text"] = hit.text;
        row["time"] = hit.ts;
        row["outgoing"] = hit.outgoing;
        row["author"] = hit.outgoing ? tr("You") : peerName(activePeer_);
        results.push_back(row);
    }
    return results;
}

void SessionController::rebuildChatList()
{
    QVector<ContactRow> rows;
    QSet<QString> known;
    if (!savedPeer().isEmpty()) {
        ContactRow saved{savedPeer(), savedChatName(), chatPreview(savedPeer()),
            store_.lastTime(savedPeer()), 0, store_.isPinned(savedPeer())};
        saved.saved = true;
        rows.push_back(saved);
        known.insert(savedPeer());
    }
    for (const QString& fp : contactFps_) {
        rows.push_back(ContactRow{fp, peerName(fp), chatPreview(fp), store_.lastTime(fp),
            store_.unreadCount(fp), store_.isPinned(fp)});
        known.insert(fp);
    }
    for (const QString& peer : store_.conversationPeers()) {
        if (peer.isEmpty() || known.contains(peer)) {
            continue;
        }
        rows.push_back(ContactRow{peer, peerName(peer), chatPreview(peer),
            store_.lastTime(peer), store_.unreadCount(peer), store_.isPinned(peer)});
    }
    contacts_.setContacts(std::move(rows));
}

QString SessionController::savedPeer() const
{
    return fingerprint_;
}

QString SessionController::savedChatName()
{
    return tr("Saved messages");
}

void SessionController::retranslate()
{
    rebuildChatList();
    conversation_.retranslate();
}

QString SessionController::attachmentLabel(const QString& type)
{
    if (type == QStringLiteral("audio")) {
        return tr("[voice]");
    }
    if (type == QStringLiteral("image")) {
        return tr("[image]");
    }
    return tr("[file]");
}

QString SessionController::chatPreview(const QString& peer) const
{
    const TranscriptStore::LastMessage last = store_.lastMessage(peer);
    if (!last.text.isEmpty()) {
        return isServiceMessage(last.type) ? systemNoteText(last.text) : last.text;
    }
    if (last.type.isEmpty()) {
        return {};
    }
    if (last.attachment.isEmpty()) {
        return attachmentLabel(last.type);
    }
    return attachmentLabel(last.type) + QLatin1Char(' ') + last.attachment;
}

QString SessionController::peerName(const QString& id) const
{
    if (!id.isEmpty() && id == fingerprint_) {
        return savedChatName();
    }
    const QString name = contactState_.value(id).name;
    if (!name.isEmpty()) {
        return name;
    }
    return shortFingerprint(id);
}

QString SessionController::contactName(const QString& fp) const
{
    return contactState_.value(fp).name;
}

void SessionController::setAvatarFromGrab(QObject* const grab)
{
    auto* const grabbed = qobject_cast<QQuickItemGrabResult*>(grab);
    if (grabbed == nullptr || grabbed->image().isNull()) {
        emit actionFailed(tr("The cropped avatar could not be read."));
        return;
    }
    setAvatarBusy(true);
    emit requestSetAvatar(grabbed->image());
}

void SessionController::setAvatarBusy(const bool busy)
{
    if (avatarBusy_ == busy) {
        return;
    }
    avatarBusy_ = busy;
    emit avatarChanged();
}

bool SessionController::hasAvatar() const
{
    return !AvatarStore::instance().image(fingerprint_).isNull();
}

void SessionController::clearAvatar()
{
    setAvatarBusy(true);
    emit requestClearAvatar();
}

void SessionController::setDisplayName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == displayName_) {
        return;
    }
    emit requestSetDisplayName(trimmed);
}

bool SessionController::canWriteTo(const QString& fp) const
{
    return contactState_.value(fp).writable;
}

QString SessionController::contactInvite(const QString& fp) const
{
    return contactState_.value(fp).invite;
}

void SessionController::renameContact(const QString& fp, const QString& name)
{
    if (fp.isEmpty()) {
        return;
    }
    const QString trimmed = name.trimmed();
    contactState_[fp].name = trimmed;
    rebuildChatList();
    emit activePeerNameChanged();
    emit requestRenameContact(fp, trimmed);
}

void SessionController::clearChat(bool forEveryone)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString peer = activePeer_;
    store_.clearPeer(peer);
    if (!forEveryone) {
        emit requestSyncChatClear(peer);
    }
    if (forEveryone) {
        emit requestClearChatForEveryone(peer);
        StoredMessage sys;
        sys.peer = peer;
        sys.type = QStringLiteral("system");
        sys.text = encodeSystemNote(QT_TR_NOOP("You cleared the chat for everyone."));
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
    }
    loadLatestWindow();
    contacts_.touch(peer, peerName(peer), chatPreview(peer), store_.lastTime(peer), false);
    refreshUnreadTotal();
}

void SessionController::deleteContact()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString peer = activePeer_;
    store_.forgetPeer(peer);
    contactFps_.removeAll(peer);
    contactState_.remove(peer);
    openConversation({});
    rebuildChatList();
    refreshUnreadTotal();
    emit requestRemoveContact(peer);
}

void SessionController::clearSavedEverywhere()
{
    store_.clearPeer(savedPeer());
    loadLatestWindow();
    contacts_.touch(savedPeer(), savedChatName(), QString(), nowMillis(), false);
    rebuildChatList();
    emit requestClearSaved();
}

bool SessionController::isBlocked(const QString& peer) const
{
    return blocked_.contains(peer);
}

QVariantList SessionController::blockedList() const
{
    QVariantList rows;
    for (const QString& peer : blocked_) {
        rows.push_back(QVariantMap{{QStringLiteral("fingerprint"), peer},
            {QStringLiteral("name"), peerName(peer)}});
    }
    return rows;
}

void SessionController::setBlocked(const QString& peer, const bool blocked)
{
    if (peer.isEmpty() || isSavedChat(peer)) {
        return;
    }
    if (blocked) {
        if (!blocked_.contains(peer)) {
            blocked_.push_back(peer);
        }
    } else {
        blocked_.removeAll(peer);
    }
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit requestSetBlocked(peer, blocked);
}

bool SessionController::contactNotifications(const QString& peer) const
{
    return contactState_.value(peer).notifications;
}

bool SessionController::contactCalls(const QString& peer) const
{
    return contactState_.value(peer).calls;
}

void SessionController::setContactNotifications(const QString& peer, const bool on)
{
    if (peer.isEmpty()) {
        return;
    }
    contactState_[peer].notifications = on;
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit requestSetContactNotifications(peer, on);
}

void SessionController::setContactCalls(const QString& peer, const bool allowed)
{
    if (peer.isEmpty()) {
        return;
    }
    contactState_[peer].calls = allowed;
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit requestSetContactCalls(peer, allowed);
}

void SessionController::unblockBeforeWriting(const QString& peer)
{
    if (peer.isEmpty() || !isBlocked(peer)) {
        return;
    }
    setBlocked(peer, false);
}

void SessionController::sendText(const QString& text)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    deliverText(text, replyTo);
}

void SessionController::sendOffered(const QString& text)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    deliverText(text, QString());
}

void SessionController::deliverText(const QString& text, const QString& replyTo)
{
    unblockBeforeWriting(activePeer_);
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "text";
    m.e2eId = newE2eId();
    m.text = text;
    m.replyTo = replyTo;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    const bool saved = isSavedChat(activePeer_);
    m.status = saved ? DeliveryStatus::Delivering : DeliveryStatus::Preparing;
    m.id = store_.append(m);
    statusById_[m.id] = m.status;
    if (saved) {
        savedSends_.insert(m.id);
    }
    showInActiveView(m, true);
    contacts_.touch(activePeer_, saved ? savedChatName() : QString(), text, m.ts, false);
    if (!saved) {
        beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("send"),
            tr("To %1").arg(peerName(activePeer_)), tr("Sending…"),
            activePeer_);
    }
    emit requestSendText(activePeer_, text, m.id, m.e2eId, replyTo);
}

StoredMessage SessionController::beginAttachmentSend(const QString& type, const QString& name,
    const qint64 size, const QString& mime, const QString& srcPath)
{
    if (activePeer_.isEmpty()) {
        return {};
    }
    unblockBeforeWriting(activePeer_);
    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = type;
    m.e2eId = newE2eId();
    m.replyTo = replyTo;
    m.attName = name;
    m.attSize = size;
    m.attMime = mime;
    m.attSrcPath = srcPath;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = isSavedChat(activePeer_) ? DeliveryStatus::Delivering
                                        : DeliveryStatus::Preparing;
    m.id = store_.append(m);
    statusById_[m.id] = m.status;
    showInActiveView(m, true);
    contacts_.touch(
        activePeer_, {}, attachmentLabel(m.type) + QLatin1Char(' ') + m.attName, m.ts, false);
    beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("file-up"),
        m.attName, tr("Sending…"), activePeer_);
    return m;
}

void SessionController::sendFile(const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        return;
    }
    const QFileInfo info(localPath);
    const StoredMessage m = beginAttachmentSend(QStringLiteral("file"), info.fileName(),
        info.size(), QMimeDatabase().mimeTypeForFile(info).name(), localPath);
    if (m.id == 0) {
        return;
    }
    emit requestSendFile(activePeer_, m.attSrcPath, m.id, m.e2eId, m.replyTo);
}

void SessionController::sendPictureFile(const QString& fileUrl)
{
    const QUrl url(fileUrl);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : fileUrl;
    const PreparedPicture picture
        = preparePicture(QImage(localPath), QFileInfo(localPath).completeBaseName());
    if (picture.isEmpty()) {
        emit actionFailed(tr("that file is not a picture this can send"));
        return;
    }
    sendPreparedPicture(picture);
}

void SessionController::sendClipboardPicture()
{
    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const PreparedPicture picture = preparePicture(
        clipboard == nullptr ? QImage() : clipboard->image(), QStringLiteral("pasted"));
    if (picture.isEmpty()) {
        emit actionFailed(tr("there is no picture in the clipboard"));
        return;
    }
    sendPreparedPicture(picture);
}

void SessionController::sendShot(QObject* const shot)
{
    auto* const held = qobject_cast<PhotoShot*>(shot);
    if (held == nullptr || held->picture().isEmpty()) {
        emit actionFailed(tr("there is no photograph to send"));
        return;
    }
    sendPreparedPicture(held->picture());
    held->discard();
}

void SessionController::sendPreparedPicture(const PreparedPicture& picture)
{
    const StoredMessage m = beginAttachmentSend(QStringLiteral("image"), picture.name,
        picture.bytes.size(), picture.mime, QString());
    if (m.id == 0) {
        return;
    }
    pictureOwners_.insert(m.e2eId, m.id);
    const bool drawable = PictureStore::instance().put(m.e2eId, picture.bytes);
    store_.setHasPicture(m.id, drawable);
    conversation_.setPictureReadyForId(m.id, drawable);
    emit requestSendPicture(
        activePeer_, picture.bytes, picture.name, picture.mime, m.id, m.e2eId, m.replyTo);
}

void SessionController::sendCallback(
    const QString& data, const QString& refMsgId, const QString& label)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString opId = QStringLiteral("bot:") + refMsgId + QStringLiteral(":") + data;
    beginOperation(opId, QStringLiteral("bot"),
        (label.isEmpty() ? data : label) + QStringLiteral(" → ") + peerName(activePeer_),
        tr("Sending…"), activePeer_);
    emit requestSendCallback(opId, activePeer_, data, refMsgId);
}

void SessionController::sendCommand(
    const QString& command, const QString& args, const QString& label)
{
    if (activePeer_.isEmpty() || command.isEmpty()) {
        return;
    }
    const QString opId = QStringLiteral("bot-command:") + command;
    beginOperation(opId, QStringLiteral("bot"),
        (label.isEmpty() ? QStringLiteral("/") + command : label) + QStringLiteral(" → ")
            + peerName(activePeer_),
        tr("Sending…"), activePeer_);
    emit requestSendCommand(opId, activePeer_, command, args);
}

void SessionController::beginEdit(qint64 localId, const QString& e2eId, const QString& text)
{
    if (replying_) {
        cancelReply();
    }
    editing_ = true;
    editingLocalId_ = localId;
    editingE2eId_ = e2eId;
    editingText_ = text;
    emit editingChanged();
}

void SessionController::beginReply(
    const QString& e2eId, const QString& previewText, const QString& sender)
{
    if (e2eId.isEmpty()) {
        return;
    }
    if (editing_) {
        cancelEdit();
    }
    replying_ = true;
    replyingE2eId_ = e2eId;
    replyingText_ = previewText;
    replyingSender_ = sender;
    emit replyingChanged();
}

void SessionController::cancelReply()
{
    if (!replying_) {
        return;
    }
    replying_ = false;
    replyingE2eId_.clear();
    replyingText_.clear();
    replyingSender_.clear();
    emit replyingChanged();
}

QVariantMap SessionController::replyPreview(const QString& e2eId) const
{
    QVariantMap info;
    info[QStringLiteral("found")] = false;
    info[QStringLiteral("localId")] = 0;
    info[QStringLiteral("text")] = QString();
    info[QStringLiteral("sender")] = QString();
    if (e2eId.isEmpty() || activePeer_.isEmpty()) {
        return info;
    }
    const StoredMessage m = store_.messageByE2e(e2eId, activePeer_);
    if (m.id == 0) {
        return info;
    }
    info[QStringLiteral("found")] = true;
    info[QStringLiteral("localId")] = m.id;
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = QStringLiteral("\xF0\x9F\x93\x8E ") + m.attName;
    }
    info[QStringLiteral("text")] = preview;
    if (m.outgoing) {
        info[QStringLiteral("sender")] = QStringLiteral("You");
    } else {
        info[QStringLiteral("sender")] = peerName(activePeer_);
    }
    return info;
}

void SessionController::commitEdit(const QString& newText)
{
    if (!editing_) {
        return;
    }
    const QString trimmed = newText.trimmed();
    if (!trimmed.isEmpty() && trimmed != editingText_) {
        store_.editContent(editingLocalId_, trimmed, {});
        conversation_.editById(editingLocalId_, trimmed, {});
        contacts_.touch(activePeer_, {}, trimmed, nowMillis(), false);
        restartDelivery(editingLocalId_);
        emit requestSendEdit(activePeer_, editingE2eId_, editingLocalId_, trimmed);
    }
    cancelEdit();
}

void SessionController::cancelEdit()
{
    if (!editing_) {
        return;
    }
    editing_ = false;
    editingLocalId_ = 0;
    editingE2eId_.clear();
    editingText_.clear();
    emit editingChanged();
}

void SessionController::deleteMessage(qint64 localId, const QString& e2eId, bool outgoing)
{
    if (activePeer_.isEmpty() || localId == 0) {
        return;
    }
    if (!e2eId.isEmpty()) {
        emit requestUnsend(e2eId);
    }
    store_.removeById(localId);
    conversation_.removeById(localId);
    statusById_.remove(localId);
    contacts_.touch(activePeer_, {}, chatPreview(activePeer_), store_.lastTime(activePeer_),
        false);
    if (outgoing && !e2eId.isEmpty()) {
        emit requestSendDelete(activePeer_, e2eId);
    }
}

void SessionController::keepThisDeviceAddress()
{
    emit requestPublishThisDeviceAddress();
}

void SessionController::useFreshAddress()
{
    emit requestPublishFreshAddress();
}

void SessionController::signLogin(const QString& challenge)
{
    if (loginSigner_) {
        QString blob;
        try {
            blob = QString::fromStdString(loginSigner_->sign(challenge.toStdString()));
        } catch (const std::exception& error) {
            const QString problem = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(
                this, [this, problem]() { emit actionFailed(problem); }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, blob]() { emit loginSigned(blob); }, Qt::QueuedConnection);
        return;
    }
    emit requestSignLoginSig(challenge);
}

void SessionController::askForContacts()
{
    emit requestContactsFromDevices();
}

void SessionController::refreshConnectionLog()
{
    emit requestConnectionLog();
}

void SessionController::clearConnectionLog()
{
    emit requestClearConnectionLog();
}

QVariantMap SessionController::describeLoginChallenge(const QString& challenge) const
{
    QVariantMap described;
    try {
        const bazarish::service::LoginConsumer consumer
            = bazarish::service::readLoginConsumer(challenge.trimmed().toStdString());
        described["ok"] = true;
        described["name"] = QString::fromStdString(consumer.name);
        QStringList places;
        for (const std::string& place : consumer.place) {
            places << QString::fromStdString(place);
        }
        described["place"] = places;
        described["role"] = QString::fromStdString(consumer.role);
    } catch (const std::exception& error) {
        described["ok"] = false;
        described["problem"] = QString::fromUtf8(error.what());
    }
    return described;
}

void SessionController::changePassphrase(const QString& passphrase)
{
    accountPassphrase_ = passphrase;
    emit requestChangePassphrase(passphrase);
}

void SessionController::startPairing()
{
    pairUri_.clear();
    pairCode_.clear();
    pairStatus_ = tr("Making an address");
    pairProgress_ = kProgressUnknown;
    pairing_ = true;
    emit pairingChanged();
    emit requestStartPairing();
}

void SessionController::stopPairing()
{
    pairing_ = false;
    pairUri_.clear();
    pairCode_.clear();
    pairStatus_.clear();
    pairProgress_ = kProgressUnknown;
    emit pairingChanged();
    emit requestStopPairing();
}

void SessionController::onPairOfferReady(const QString& uri, const QString& code)
{
    pairUri_ = uri;
    pairCode_ = code;
    emit pairingChanged();
}

void SessionController::onPairStage(const QString& status, const double progress)
{
    if (!pairing_) {
        return;
    }
    pairStatus_ = status;
    pairProgress_ = progress;
    emit pairingChanged();
}

void SessionController::onPairFinished(const bool ok, const QString& status)
{
    if (!pairing_) {
        return;
    }
    pairing_ = false;
    pairUri_.clear();
    pairCode_.clear();
    pairStatus_ = status;
    pairProgress_ = kProgressUnknown;
    emit pairingChanged();
    emit pairingFinished(ok);
}

void SessionController::exportAccount(const QString& fileUrl, const QString& password)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        return;
    }
    beginOperation(QStringLiteral("export"), QStringLiteral("account"),
        tr("Exporting your backup"), tr("Waiting for this account…"));
    emit requestExport(localPath, password);
}

QString SessionController::shortFingerprint(const QString& fp) const
{
    if (fp.size() <= 14) {
        return fp;
    }
    return fp.left(8) + "…" + fp.right(4);
}

void SessionController::setChatFilter(const QString& text)
{
    contactsProxy_.setFilterFixedString(text.trimmed());
}

void SessionController::pinChat(const QString& peer, bool pinned)
{
    if (peer.isEmpty()) {
        return;
    }
    store_.setPinned(peer, pinned);
    rebuildChatList();
    emit requestSyncChatPin(peer, pinned);
}

void SessionController::beginOperation(const QString& id, const QString& kind, const QString& title,
    const QString& status, const QString& peer, const QString& cancelId)
{
    OperationRow row;
    row.id = id;
    row.kind = kind;
    row.title = title;
    row.status = status;
    row.state = eOpRunning;
    row.startedAt = nowMillis();
    row.peer = peer;
    row.cancelId = cancelId;
    operations_.upsert(row);
    emit operationsChanged();
}

void SessionController::updateOperation(
    const QString& id, const QString& status, const QString& detail, double progress)
{
    operations_.update(id, status, detail, progress, eOpRunning);
}

void SessionController::finishOperation(const QString& id, bool ok, const QString& finalStatus)
{
    if (operations_.indexOf(id) < 0) {
        return;
    }
    operations_.update(id, finalStatus, {}, -1.0, ok ? eOpDone : eOpFailed);
    emit operationsChanged();
    QTimer::singleShot(ok ? 3500 : 6000, this, [this, id]() {
        operations_.remove(id);
        emit operationsChanged();
    });
}

void SessionController::generatePersonalKey()
{
    beginOperation(QStringLiteral("dest-key"), QStringLiteral("dest"),
        tr("Creating your destination key"), tr("Queued…"));
    emit requestGeneratePersonalKey();
}

void SessionController::loadPersonalKey(const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (!localPath.isEmpty()) {
        emit requestLoadPersonalKey(localPath);
    }
}

void SessionController::replacePersonalKey()
{
    emit requestReplacePersonalKey();
}

void SessionController::setAliasBinding(const QString& alias, const bool on)
{
    emit requestSetAliasBinding(alias, on);
}

void SessionController::retryRoutingTo(const QString& peer)
{
    emit requestRetryRoutingTo(peer);
}

void SessionController::setDelegationDays(const int days)
{
    const int bounded = std::clamp(days, minDelegationDays(), maxDelegationDays());
    if (delegationDays_ == bounded) {
        return;
    }
    delegationDays_ = bounded;
    emit requestSetDelegationDays(bounded);
    emit delegationDaysChanged();
}

void SessionController::setAcceptCalls(const bool on)
{
    if (acceptCalls_ == on) {
        return;
    }
    acceptCalls_ = on;
    emit requestSetAcceptCalls(on);
    emit acceptCallsChanged();
}

void SessionController::setSendReceipts(const bool on)
{
    if (sendReceipts_ == on) {
        return;
    }
    sendReceipts_ = on;
    emit requestSetSendReceipts(on);
    emit sendReceiptsChanged();
}

void SessionController::publishPersonalDest()
{
    i2pBusy_ = true;
    flashOnNextStatus_ = true;
    emit i2pStatusChanged();
    emit requestPublishPersonalDest();
}

void SessionController::disablePersonalDest()
{
    i2pBusy_ = true;
    flashOnNextStatus_ = true;
    emit i2pStatusChanged();
    emit requestDisablePersonalDest();
}

void SessionController::refreshI2pStatus()
{
    emit requestRefreshI2pStatus();
}

void SessionController::onI2pKeyState(const bool hasKey, const QString& address)
{
    i2pHasKey_ = hasKey;
    i2pAddress_ = address;
    emit i2pStatusChanged();
}

void SessionController::onI2pStatus(const bool hasKey, const bool delegated, const bool live,
    const QString& address, const QString& summary, const qint64 transientExpires,
    const QString& serverState)
{
    i2pBusy_ = false;
    const bool flash = flashOnNextStatus_;
    flashOnNextStatus_ = false;
    i2pServerState_ = serverState;
    i2pHasKey_ = hasKey;
    i2pEnabled_ = delegated;
    i2pActive_ = live;
    i2pAddress_ = address;
    i2pStatusText_ = summary;
    i2pTransientExpires_ = transientExpires;
    emit i2pStatusChanged();
    if (flash) {
        emit i2pAnswered();
    }
}

void SessionController::refreshStorageUsage()
{
    emit requestRefreshStorageUsage();
}

void SessionController::refreshDevices()
{
    emit requestRefreshDevices();
}

void SessionController::forgetDevice(const QString& clientId)
{
    emit requestForgetDevice(clientId);
}

void SessionController::closeAccountOnServer()
{
    emit requestCloseAccountOnServer();
}

void SessionController::forwardMessage(const QString& e2eId, const QString& toPeer)
{
    if (e2eId.isEmpty() || toPeer.isEmpty()) {
        return;
    }
    const StoredMessage source = store_.messageByE2e(e2eId, activePeer_);
    if (source.id == 0) {
        return;
    }
    unblockBeforeWriting(toPeer);
    StoredMessage m;
    m.peer = toPeer;
    m.outgoing = true;
    m.type = source.type;
    m.e2eId = newE2eId();
    m.text = source.text;
    m.forwarded = true;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::Preparing;

    if (source.type == QStringLiteral("audio")) {
        const QByteArray audio = store_.media(QStringLiteral("voice:") + source.e2eId);
        if (audio.isEmpty()) {
            emit actionFailed(tr("this voice message is no longer on this device"));
            return;
        }
        m.attMime = source.attMime;
        m.attSize = audio.size();
        m.attDurationMs = source.attDurationMs;
        m.attWave = source.attWave;
        m.id = store_.append(m);
        forwardShown(m, toPeer, tr("[voice]"));
        emit requestSendVoice(toPeer, audio, m.attDurationMs, m.id, m.e2eId, QString(), true);
        return;
    }
    if (source.type == QStringLiteral("text")) {
        m.id = store_.append(m);
        forwardShown(m, toPeer, m.text);
        emit requestSendText(toPeer, m.text, m.id, m.e2eId, QString(), true);
        return;
    }
    m.attName = source.attName;
    m.attMime = source.attMime;
    m.attSize = source.attSize;
    if (source.type == QStringLiteral("image")) {
        QByteArray picture = store_.media(QStringLiteral("picture:") + source.e2eId);
        if (picture.isEmpty() && !source.savedPath.isEmpty()) {
            QFile saved(source.savedPath);
            if (saved.open(QIODevice::ReadOnly)) {
                picture = saved.readAll();
            }
        }
        if (picture.isEmpty()) {
            emit actionFailed(tr("save this to your device first, then it can be forwarded"));
            return;
        }
        m.id = store_.append(m);
        forwardShown(m, toPeer, m.attName.isEmpty() ? tr("[image]") : m.attName);
        emit requestSendPicture(toPeer, picture, m.attName, m.attMime, m.id, m.e2eId, QString());
        return;
    }
    const QString path = source.savedPath;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        emit actionFailed(tr("save this to your device first, then it can be forwarded"));
        return;
    }
    m.attSrcPath = path;
    m.id = store_.append(m);
    forwardShown(m, toPeer, m.attName.isEmpty() ? tr("[file]") : m.attName);
    emit requestSendFile(toPeer, path, m.id, m.e2eId, QString());
}

void SessionController::forwardShown(
    const StoredMessage& m, const QString& toPeer, const QString& preview)
{
    statusById_[m.id] = DeliveryStatus::Preparing;
    if (toPeer == activePeer_) {
        showInActiveView(m, true);
    }
    const bool saved = isSavedChat(toPeer);
    if (saved) {
        savedSends_.insert(m.id);
    }
    contacts_.touch(toPeer, saved ? savedChatName() : peerName(toPeer), preview, m.ts, false);
    if (!saved) {
        beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("send"),
            tr("To %1").arg(peerName(toPeer)), tr("Forwarding…"), toPeer);
    }
}

void SessionController::requestPicturesFor(const QList<StoredMessage>& messages)
{
    for (const StoredMessage& message : messages) {
        if (!message.hasPicture || message.e2eId.isEmpty()) {
            continue;
        }
        pictureOwners_.insert(message.e2eId, message.id);
        if (PictureStore::instance().has(message.e2eId)) {
            continue;
        }
        const QByteArray bytes = store_.media(QStringLiteral("picture:") + message.e2eId);
        if (bytes.isEmpty()) {
            store_.setHasPicture(message.id, false);
            conversation_.setPictureReadyForId(message.id, false);
            continue;
        }
        if (!PictureStore::instance().put(message.e2eId, bytes)) {
            store_.setHasPicture(message.id, false);
            conversation_.setPictureReadyForId(message.id, false);
        }
    }
}

void SessionController::savePictureAs(const QString& e2eId, const QString& fileUrl)
{
    const QString path = QUrl(fileUrl).toLocalFile();
    const QByteArray bytes = PictureStore::instance().bytes(e2eId);
    if (path.isEmpty() || bytes.isEmpty()) {
        emit actionFailed(tr("This picture is not here to save."));
        return;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size()) {
        emit actionFailed(tr("Could not write %1").arg(path));
        return;
    }
    emit actionOk(tr("Picture saved"));
}

void SessionController::copyPicture(const QString& e2eId)
{
    const QImage picture = PictureStore::instance().image(e2eId);
    if (picture.isNull()) {
        emit actionFailed(tr("This picture is not here to copy."));
        return;
    }
    QClipboard* const clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        emit actionFailed(tr("There is no clipboard to copy to."));
        return;
    }
    clipboard->setImage(picture);
    emit actionOk(tr("Picture copied"));
}

QUrl SessionController::defaultPictureSaveUrl(const QString& e2eId, const QString& name) const
{
    const QByteArray bytes = PictureStore::instance().bytes(e2eId);
    const QString suffix = bytes.startsWith(QByteArray::fromHex("89504E47"))
        ? QStringLiteral(".png") : QStringLiteral(".jpg");
    const QString base = name.isEmpty() ? QStringLiteral("picture") : QFileInfo(name).completeBaseName();
    return defaultSaveUrl(base + suffix);
}

void SessionController::onDevicesReady(const QVariantList& devices)
{
    devices_ = devices;
    emit devicesChanged();
}

void SessionController::onStorageUsageReady(
    const bool mailboxOk, const qulonglong mailboxUsed, const qulonglong mailboxQuota)
{
    if (!mailboxOk) {
        return;
    }
    storageMailboxOk_ = true;
    storageMailboxUsed_ = mailboxUsed;
    storageMailboxQuota_ = mailboxQuota;
    storageUpdatedAtMs_ = nowMillis();
    emit storageChanged();
}

QVariantMap SessionController::storageInfo() const
{
    QVariantMap m;
    m[QStringLiteral("mailboxOk")] = storageMailboxOk_;
    m[QStringLiteral("mailboxUsed")] = static_cast<qulonglong>(storageMailboxUsed_);
    m[QStringLiteral("mailboxQuota")] = static_cast<qulonglong>(storageMailboxQuota_);
    m[QStringLiteral("updatedAt")] = storageUpdatedAtMs_;
    m[QStringLiteral("everFetched")] = (storageUpdatedAtMs_ > 0);
    return m;
}

void SessionController::measureDeviceStorage()
{
    const QVector<ChatWeight> weights = store_.chatWeights();
    QVariantList chats;
    QVector<ChatWeight> ordered = weights;
    std::sort(ordered.begin(), ordered.end(), [](const ChatWeight& a, const ChatWeight& b) {
        return a.rowBytes + a.mediaBytes > b.rowBytes + b.mediaBytes;
    });
    for (const ChatWeight& weight : ordered) {
        QVariantMap chat;
        chat[QStringLiteral("peer")] = weight.peer;
        chat[QStringLiteral("name")] = peerName(weight.peer);
        chat[QStringLiteral("messages")] = weight.messages;
        chat[QStringLiteral("bytes")] = weight.rowBytes + weight.mediaBytes;
        chat[QStringLiteral("mediaCount")] = weight.mediaCount;
        chats << chat;
    }
    const DatabaseFootprint footprint = store_.footprint();
    deviceStorage_.clear();
    deviceStorage_[QStringLiteral("busy")] = deviceStorageBusy_;
    deviceStorage_[QStringLiteral("measuredAt")] = nowMillis();
    deviceStorage_[QStringLiteral("fileBytes")] = footprint.fileBytes;
    deviceStorage_[QStringLiteral("freeBytes")] = footprint.freeBytes;
    deviceStorage_[QStringLiteral("chats")] = chats;
    emit deviceStorageChanged();
}

void SessionController::trimChat(const QString& peer, const int keep)
{
    if (peer.isEmpty()) {
        return;
    }
    runTrim(peer, keep);
}

void SessionController::trimEveryChat(const int keep)
{
    runTrim({}, keep);
}

void SessionController::beginStorageWork(const QString& what, const std::function<void()>& work)
{
    deviceStorageBusy_ = true;
    deviceStorage_[QStringLiteral("busy")] = true;
    deviceStorage_[QStringLiteral("busyWhat")] = what;
    emit deviceStorageChanged();
    QTimer::singleShot(kBusyPaintDelayMs, this, work);
}

void SessionController::endStorageWork()
{
    deviceStorageBusy_ = false;
    measureDeviceStorage();
}

void SessionController::compactDatabase()
{
    if (deviceStorageBusy_) {
        return;
    }
    beginStorageWork(tr("Compacting the database"), [this]() {
        QString reason;
        const bool rebuilt = store_.rebuild(reason);
        const qint64 before = deviceStorage_.value(QStringLiteral("fileBytes")).toLongLong();
        endStorageWork();
        if (!rebuilt) {
            emit actionFailed(tr("The database was not compacted: %1").arg(reason));
            return;
        }
        const qint64 after = deviceStorage_.value(QStringLiteral("fileBytes")).toLongLong();
        emit actionOk(tr("The database was compacted. %1 came back to the disk")
                          .arg(humanBytes(std::max<qint64>(0, before - after))));
    });
}

void SessionController::runTrim(const QString& peer, const int keep)
{
    if (deviceStorageBusy_) {
        return;
    }
    beginStorageWork(tr("Trimming and compacting the database"),
        [this, peer, keep]() {
            qint64 removed = 0;
            try {
                removed = peer.isEmpty() ? store_.pruneEveryChatToLatest(keep)
                                         : store_.pruneToLatest(peer, keep);
            } catch (const std::exception& error) {
                endStorageWork();
                emit actionFailed(QString::fromUtf8(error.what()));
                return;
            }
            QString reason;
            const bool rebuilt = store_.rebuild(reason);
            loadLatestWindow();
            rebuildChatList();
            refreshUnreadTotal();
            endStorageWork();
            if (rebuilt) {
                emit actionOk(tr("Removed %1 messages and compacted the database")
                        .arg(removed));
                return;
            }
            emit actionFailed(tr("Removed %1 messages, but the space has not been returned to "
                                 "the disk: %2. Trimming again returns it.")
                    .arg(removed).arg(reason));
        });
}

void SessionController::onOpened(
    const QString& fingerprint, const QString& displayName, bool connected)
{
    fingerprint_ = fingerprint;
    displayName_ = displayName;
    connected_ = connected;
    emit identityChanged();
    emit connectedChanged();
    if (const bool nowOnline = connected && startOnline_; online_ != nowOnline) {
        online_ = nowOnline;
        emit onlineChanged();
    }
}

void SessionController::onConnectionChanged(bool connected, const QString& connectionNote)
{
    if (connecting_) {
        connecting_ = false;
        finishOperation(kConnectOperationId, connected,
            connected ? tr("Connected") : connectionNote);
    }
    connectPhase_.clear();
    connectError_ = connected ? QString() : connectionNote;
    emit connectStateChanged();
    connected_ = connected;
    emit connectedChanged();
    if (online_ != connected) {
        online_ = connected;
        emit onlineChanged();
    }
}

void SessionController::goOnline()
{
    startOnline_ = true;
    if (!online_) {
        online_ = true;
        emit onlineChanged();
    }
    emit requestSetSync(true);
}

void SessionController::rebuildI2pLinks()
{
    linksRebuilding_ = true;
    rebuildGrace_.start();
    emit requestRebuildI2p();
}

void SessionController::goOffline()
{
    if (online_) {
        online_ = false;
        emit onlineChanged();
    }
    if (reachable_) {
        reachable_ = false;
        emit reachableChanged();
    }
    emit requestSetSync(false);
}

void SessionController::onSyncReachable(const bool ok)
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
    emit reachableChanged();
}

void SessionController::onApprovalState(const bool pending, const QString& note)
{
    if (awaitingApproval_ == pending && approvalNote_ == note) {
        return;
    }
    awaitingApproval_ = pending;
    approvalNote_ = note;
    emit approvalChanged();
}

void SessionController::restartDelivery(qint64 localId)
{
    statusById_[localId] = DeliveryStatus::Preparing;
    store_.updateStatus(localId, DeliveryStatus::Preparing);
    conversation_.setStatusForId(localId, DeliveryStatus::Preparing);
    conversation_.setErrorForId(localId, {});
}

void SessionController::bumpStatus(qint64 localId, int status)
{
    const int current = statusById_.value(localId, DeliveryStatus::Preparing);
    if (status != DeliveryStatus::Failed && status <= current) {
        return;
    }
    statusById_[localId] = status;
    store_.updateStatus(localId, status);
    conversation_.setStatusForId(localId, status);
}

void SessionController::onSendProgress(qint64 localId, int state)
{
    if (state == DeliveryStatus::AtRecipientServer && savedSends_.contains(localId)) {
        bumpStatus(localId, DeliveryStatus::Delivered);
        finishOperation(QStringLiteral("send:") + QString::number(localId), true, tr("Saved"));
        return;
    }
    bumpStatus(localId, state);
    if (state == DeliveryStatus::AtRecipientServer) {
        finishOperation(QStringLiteral("send:") + QString::number(localId), true,
            tr("Handed to the recipient's server"));
    }
}

void SessionController::onSendResult(qint64 localId, bool ok, const QString& error)
{
    if (ok) {
        conversation_.setErrorForId(localId, {});
        if (savedSends_.remove(localId)) {
            bumpStatus(localId, DeliveryStatus::Delivered);
        }
        return;
    }
    savedSends_.remove(localId);
    bumpStatus(localId, DeliveryStatus::Failed);
    conversation_.setErrorForId(localId, error);
    finishOperation(QStringLiteral("send:") + QString::number(localId), false,
        tr("Failed: %1").arg(error));
}

void SessionController::onSendPhase(qint64 localId, const QString& phase)
{
    const QString human = humanDeliveryPhase(phase);
    updateOperation(QStringLiteral("send:") + QString::number(localId), human);
    if (isRetryPhase(phase)) {
        conversation_.setErrorForId(localId, human);
    }
}

void SessionController::resendText(qint64 localId, const QString& text, const QString& e2eId)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    restartDelivery(localId);
    const QString replyTo = store_.messageByE2e(e2eId, activePeer_).replyTo;
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("send"),
        tr("To %1").arg(peerName(activePeer_)), tr("Sending again…"),
        activePeer_);
    emit requestSendText(activePeer_, text, localId, e2eId, replyTo);
}

void SessionController::resendFile(qint64 localId, const QString& e2eId)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString srcPath = store_.sourcePathFor(localId);
    if (srcPath.isEmpty() || !QFileInfo::exists(srcPath)) {
        emit resendFilePickRequested();
        return;
    }
    restartDelivery(localId);
    const QString replyTo = store_.messageByE2e(e2eId, activePeer_).replyTo;
    const StoredMessage stored = store_.messageByE2e(e2eId, activePeer_);
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("file-up"),
        stored.attName.isEmpty() ? tr("file") : stored.attName,
        tr("Sending again…"), activePeer_);
    emit requestSendFile(activePeer_, srcPath, localId, e2eId, replyTo);
}

void SessionController::resendVoice(qint64 localId, const QString& e2eId)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QByteArray audio = store_.media(QStringLiteral("voice:") + e2eId);
    if (audio.isEmpty()) {
        emit actionFailed(tr("this voice message is no longer on this device"));
        return;
    }
    const StoredMessage stored = store_.messageByE2e(e2eId, activePeer_);
    restartDelivery(localId);
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("send"),
        tr("To %1").arg(peerName(activePeer_)), tr("Sending again…"),
        activePeer_);
    emit requestSendVoice(
        activePeer_, audio, stored.attDurationMs, localId, e2eId, stored.replyTo, stored.forwarded);
}

void SessionController::resendPicture(qint64 localId, const QString& e2eId)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QByteArray picture = store_.media(QStringLiteral("picture:") + e2eId);
    if (picture.isEmpty()) {
        emit actionFailed(tr("this picture is no longer on this device"));
        return;
    }
    const StoredMessage stored = store_.messageByE2e(e2eId, activePeer_);
    restartDelivery(localId);
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("file-up"),
        stored.attName.isEmpty() ? tr("image") : stored.attName,
        tr("Sending again…"), activePeer_);
    emit requestSendPicture(activePeer_, picture, stored.attName, stored.attMime, localId, e2eId,
        stored.replyTo);
}

void SessionController::markOutgoingRead(const QString& peer, qint64 uptoId)
{
    store_.markOutgoingReadUpTo(peer, uptoId, DeliveryStatus::Delivered,
        DeliveryStatus::AtRecipientServer, DeliveryStatus::AtRecipientServer);
    if (peer == activePeer_) {
        for (const qint64 id : conversation_.markDeliveredThrough(uptoId)) {
            statusById_[id] = DeliveryStatus::Delivered;
        }
    }
}

void SessionController::markReadThroughRow(int row)
{
    if (activePeer_.isEmpty() || row < 0) {
        return;
    }
    qint64 id = 0;
    qint64 sentAt = 0;
    QString e2eId;
    if (!conversation_.newestIncomingThrough(row, id, e2eId, sentAt)) {
        return;
    }
    const qint64 prevAcked = lastReadAckedId_.value(activePeer_, 0);
    if (id <= prevAcked) {
        return;
    }
    lastReadAckedId_[activePeer_] = id;
    store_.setLastReadId(activePeer_, id);
    contacts_.setUnread(activePeer_, store_.unreadCount(activePeer_));
    pendingReadSync_[activePeer_] = sentAt;
    readSyncTimer_.start(kReadSyncIdleMs);
    if (!sendReceipts_ || !online_) {
        return;
    }
    emit requestSendReceipt(activePeer_, e2eId);
}

void SessionController::flushReadSync()
{
    readSyncTimer_.stop();
    if (!online_) {
        pendingReadSync_.clear();
        return;
    }
    for (auto it = pendingReadSync_.constBegin(); it != pendingReadSync_.constEnd(); ++it) {
        emit requestSyncRead(it.key(), it.value());
    }
    pendingReadSync_.clear();
}

void SessionController::onOpBegin(
    const QString& opId, const QString& kind, const QString& title, const QString& status)
{
    beginOperation(opId, kind, title, status);
}

void SessionController::onOpDone(const QString& opId, bool ok, const QString& status)
{
    finishOperation(opId, ok, status);
}

}  // namespace bazarish::app
