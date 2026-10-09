// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "Session.hpp"
#include "AccountDb.hpp"
#include "TranscriptStore.hpp"
#include "VoiceNote.hpp"
#include "CallTones.hpp"

#include <QObject>
#include <QSortFilterProxyModel>
#include <QSet>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class QTimer;

namespace bazarish::app {

inline constexpr double kProgressUnknown = -1.0;

struct ContactState {
    enum Request { eAnswered, eWaiting, eAccepting };

    QString fingerprint;
    QString name;
    QString invite;
    bool writable = false;
    bool notifications = true;
    bool calls = true;
    Request request = eAnswered;
};

struct ResolvedContactAddQueue;
struct AliasErrandQueue;

class SessionWorker : public QObject {
    Q_OBJECT
public:
    ~SessionWorker() override;

private:
    template <typename Work>
    void withSession(Work&& work)
    {
        if (!session_) {
            return;
        }
        try {
            work();
        } catch (const std::exception& error) {
            emit actionFailed(QString::fromUtf8(error.what()));
        }
    }

public slots:
    void openAccount(const QString& dir, const QString& passphrase, bool startOnline);
    void forgetPendingAdd(const QString& opId);
    void connectAndRegister(const QStringList& facadeUrls, const QString& serverFp,
        const QStringList& reseedUrls);
    void sync();
    void setSyncEnabled(bool on);
    void rebuildI2pLinks();
    void sendText(const QString& peer, const QString& text, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void sendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& e2eId, const QString& replyTo);
    void sendPicture(const QString& peer, const QByteArray& bytes, const QString& name,
        const QString& mime, qint64 localId, const QString& e2eId, const QString& replyTo);
    void sendVoice(const QString& peer, const QByteArray& opus, qint64 durationMs, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void sendReceipt(const QString& peer, const QString& refId);
    void ackPending(const QString& pendingId);
    void sendReaction(const QString& peer, const QString& refId, const QString& emoji);
    void sendCallback(
        const QString& opId, const QString& peer, const QString& data, const QString& ref);
    void sendCommand(
        const QString& opId, const QString& peer, const QString& command, const QString& args);
    void sendEdit(const QString& peer, const QString& refId, qint64 localId, const QString& text);
    void sendDelete(const QString& peer, const QString& refId);
    void dropSentFile(const QString& refId);
    void setAvatar(const QImage& image);
    void clearAvatar();
    void setDisplayName(const QString& name);
    void renameContact(const QString& peer, const QString& name);
    void removeContact(const QString& peer);
    void clearSaved();
    void setBlocked(const QString& peer, bool blocked);
    void setContactNotifications(const QString& peer, bool on);
    void setContactCalls(const QString& peer, bool allowed);
    void syncChatPin(const QString& peer, bool pinned);
    void syncRead(const QString& peer, qint64 sentAtMs);
    void syncChatClear(const QString& peer);
    void emitSettings();
    void clearChatForEveryone(const QString& peer);
    void addByInvite(const QString& uri, const QString& intro, const QString& opId,
        const QString& requestId);
    void addByAlias(const QString& alias, const QString& intro, const QString& opId);
    void acceptContact(const QString& peer);
    void requestInvite();
    void signLogin(const QString& challenge);
    void connectionLog();
    void clearConnectionLog();
    void askDevicesForContacts();
    void noteCommandDone();
    // Stops everything this worker owns and closes the account.
    void shutdown();
    void saveAttachment(const QString& peer, const QString& e2eId, const QString& destPath, qint64 token);
    void exportAccount(const QString& path, const QString& password);
    void startPairing();
    void stopPairing();
    void changePassphrase(const QString& passphrase);
    void activateAliasServicing();
    void generatePersonalKey();
    void loadPersonalKey(const QString& path);
    void replacePersonalKey();
    void setAliasBinding(const QString& alias, bool on);
    void retryRoutingTo(const QString& peer);
    void setAcceptCalls(bool accept);
    void setSendReceipts(bool on);
    void setDelegationDays(int days);
    void cancelTransfer(const QString& e2eId);
    void publishPersonalDest();
    void disablePersonalDest();
    void refreshI2pStatus();
    void refreshContacts();
    void publishThisDeviceAddress();
    void publishFreshAddress();
    void refreshStorageUsage();
    void refreshDevices();
    void forgetDevice(const QString& clientId);
    void closeAccountOnServer();
    void startCall(const QString& peer);
    void acceptCall(const QString& callId);
    void declineCall(const QString& callId);
    void endCall();
    void setCallMuted(bool muted);

signals:
    void commandFinished();
    void addressMismatch(const QString& servedHost, const QString& ourHost);
    void loginSignerReady(std::shared_ptr<bazarish::client::LoginSigner> signer);
    void opBegin(const QString& opId, const QString& kind, const QString& title,
        const QString& status);
    void opDone(const QString& opId, bool ok, const QString& status);
    void opProgress(const QString& opId, const QString& status);
    void pairOfferReady(const QString& uri, const QString& code);
    void pairStage(const QString& status, double progress);
    void pairFinished(bool ok, const QString& status);
    void accountClosed(bool ok, const QString& error);
    void accountSettings(bool acceptCalls, bool sendReceipts);
    void aliasHoldings(const QVariantList& rows, const QString& note);
    void routingTold(const QString& peer, bool delivered);
    void aliasActivationDone(bool ok, const QString& text);
    void opened(const QString& fingerprint, const QString& displayName, bool connected);
    void renamed(const QString& newName);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& connectionNote);
    void connectProgress(int percent, const QString& phase);
    void messageReceived(const QVariantMap& message);
    void contactsRefreshed(const QVector<ContactState>& contacts,
        const QStringList& blocked);
    void avatarReady(const QString& fingerprint, const QByteArray& data);
    void sendProgress(qint64 localId, int state);
    void sendResult(qint64 localId, bool ok, const QString& error);
    void sendPhase(qint64 localId, const QString& phase);
    void uploadProgress(qint64 localId, qint64 sent, qint64 total);
    void downloadProgress(qint64 token, qint64 received, qint64 total);
    void servedProgress(const QString& peer, const QString& e2eId, qint64 sent, qint64 total);
    void transferStage(const QString& peer, const QString& e2eId, const QString& stage);
    void servedFinished(const QString& peer, const QString& e2eId, bool ok,
        const QString& error);
    void downloadFinished(qint64 token, bool ok, const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void botActionDone(const QString& opId, bool ok, const QString& error);
    void contactRequestSent(
        const QString& fingerprint, const QString& intro, const QString& requestId);
    void contactAddStage(const QString& opId, const QString& status);
    void contactAddRateLimited(
        const QString& opId, const QString& fingerprint, const QString& requestId);
    void contactAddDone(const QString& opId, bool ok, const QString& status);
    void contactAlreadyKnown(const QString& opId, const QString& fingerprint);
    void contactRequestUnconfirmed(const QString& opId);
    void contactAddResumed(
        const QString& opId, const QString& uri, const QString& intro, const QString& requestId);
    void contactAccepted(const QString& peer, bool ok, const QString& reason);
    void inviteReady(const QString& uri);
    void inviteUnavailable(const QString& reason);
    void loginSigned(const QString& blob);
    void connectionLogReady(const QVariantList& lines);
    void stopped();
    void syncReachable(bool ok);
    void approvalState(bool pending, const QString& note);
    void facadeInfo(const QString& activeUrl, const QStringList& configured,
        const QString& serverFp, const QStringList& reseeds);
    void i2pStatus(bool hasKey, bool delegated, bool live, const QString& address,
        const QString& summary, qint64 transientExpires, const QString& serverState);
    void i2pKeyState(bool hasKey, const QString& address);
    void i2pServedAddress(const QString& address);
    void devicesReady(const QVariantList& devices);
    void storageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota);
    void serverHello(const QString& reason, const QString& message, const QStringList& links);
    void callStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, bool peerRinging, qint64 connectedAtMs, float inputLevel,
        float outputLevel);
    void callLogged(const QString& peer, bool incoming, int outcome, qint64 durationSec);

private:
    QString beginOp(const QString& kind, const QString& title, const QString& status);
    QString coreText(const QString& reported);
    void reportPairing(const bazarish::client::Session::PairingEvent& event);
    int opSeq_ = 0;
    void startMaintenance();
    void startReceiving();
    void emitFacadeInfo();
    void emitContacts();
    void emitCallState();
    void reconcileCallTimer();
    void flushCallLog();
    void startContactAdd(bool byAlias, const QString& uriOrAlias, const QString& intro,
        const QString& opId, const QString& requestId = {});
    void resumePendingAdds();
    bool addsResumed_ = false;
    bazarish::client::DeliveryWatch contactRequestWatch(const QString& opId,
        const QString& fingerprint, const QString& intro, const QString& requestId,
        bool recordedHere);
    void finishContactRequest(const QString& opId, const QString& fingerprint,
        const QString& intro, const QString& requestId, bool recordedHere,
        const bazarish::client::OutboundCourier::Outcome& outcome);
    void drainMailbox();
    void maintain();
    void refreshCalls();
    void drainResolvedAdds();
    void startAliasErrand(bool byHand);
    void drainAliasErrands();
    std::unique_ptr<bazarish::client::Session> session_;
    std::shared_ptr<ResolvedContactAddQueue> resolvedAdds_;
    std::shared_ptr<AliasErrandQueue> aliasErrands_;
    bool aliasErrandRunning_ = false;
    bool aliasErrandByHand_ = false;
    bool routingFanoutTried_ = false;
    QTimer* maintenanceTimer_ = nullptr;
    QTimer* callTimer_ = nullptr;
    std::thread eventWaiter_;
    std::shared_ptr<std::atomic<bool>> eventWaiterRunning_;
    void startEventWaiter();
    void stopEventWaiter();
    std::thread errands_;
    std::mutex errandMutex_;
    std::condition_variable errandWake_;
    struct Errand {
        std::string pendingId;
        std::string deliveryId;
        bazarish::Bytes sealed;
        std::string kind;
    };
    std::deque<Errand> errandQueue_;
    bool errandsRunning_ = false;
    void startErrands();
    void stopErrands();
    void queueErrand(Errand errand);
    void queueAck(const std::string& pendingId);

    void settleDrain();
    std::mutex drainMutex_;
    std::condition_variable drainDone_;
    bool drainSettled_ = true;
    qint64 lastTransientCheckMs_ = 0;
    qint64 lastAliasServiceMs_ = 0;
    qint64 lastApprovalCheckMs_ = 0;
    qint64 lastRegisterAttemptMs_ = 0;
    std::atomic<bool> downloadsCancelled_{false};
    QThreadPool downloadPool_;
};

}  // namespace bazarish::app
