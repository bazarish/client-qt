// Bazarish project (c) 2026
#pragma once

#include "AccountEvents.hpp"
#include "Session.hpp"
#include "AccountDb.hpp"
#include "I2pRouter.hpp"
#include "LinkFailure.hpp"
#include "MailWaitGate.hpp"
#include "OverviewWatch.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class QEvent;
class QTimer;

namespace bazarish::app {

struct ResolvedContactAddQueue;
struct AliasErrandQueue;

class SessionWorker : public QObject {
    Q_OBJECT
public:
    ~SessionWorker() override;

    bool event(QEvent* event) override;

    // Any thread: a mail wait resting after a failure asks again at once.
    // Any thread: takes the account's I2P links down, so a call blocked on one returns at once.
    void releaseLinksNow();
    // Any thread: returns once the account's presence has told its watchers it is offline.
    void leavePresenceNow();

private:
    // The open session: a slot that runs once it is gone fails as any refusal does.
    bazarish::client::Session& openSession();

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
    void connectAndRegister(const QStringList& facades, const QString& serverFp,
        const QStringList& reseedUrls);
    void sync();
    void setSyncEnabled(bool on);
    void rebuildI2pLinks();
    void watchUnsent(const QVariantList& rows);
    void sendText(const QString& peer, const QString& text, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void sendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void sendFileBytes(const QString& peer, const QByteArray& bytes, const QString& name,
        qint64 localId, const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void sendPicture(const QString& peer, const QByteArray& bytes, const QString& name,
        const QString& mime, qint64 localId, const QString& e2eId, const QString& replyTo,
        bool forwarded = false);
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
    void setAvatar(const QByteArray& bytes);
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
    void sendChatTimer(const QString& peer, qint64 seconds);
    void addByInvite(const QString& uri, const QString& intro, const QString& opId,
        const QString& requestId);
    void addByAlias(const QString& alias, const QString& intro, const QString& opId,
        const QString& requestId);
    void acceptContact(const QString& peer);
    void signLogin(const QString& challenge);
    void watchConnectionLog(bool on);
    void clearConnectionLog();
    void askDevicesForContacts();
    void noteCommandDone();
    void shutdown();
    void saveAttachment(const QString& peer, const QString& e2eId,
        const QString& announcedSha256, const QString& destPath, qint64 token);
    void exportAccount(const QString& path, const QString& password, bool primary);
    void startPairing(bool primary);
    void stopPairing();
    void activateAliasServicing();
    void generatePersonalKey();
    void loadPersonalKey(const QString& path);
    void replacePersonalKey();
    void setAliasBinding(const QString& alias, bool on);
    void setAliasWord(const QString& alias, const QString& word);
    void retryRoutingTo(const QString& peer);
    void setAcceptCalls(bool accept);
    void setSendReceipts(bool on);
    void setShowPresence(bool on);
    void setPresenceFocused(bool focused);
    void setPresenceAway(bool away);
    void setPresenceOrder(const QStringList& chatOrder);
    void watchPresence(const QStringList& peers);
    void presenceTyped(const QString& peer);
    void presenceStoppedTyping(const QString& peer);
    void setDelegationDays(int days);
    void cancelTransfer(const QString& e2eId);
    void publishPersonalDest();
    void disablePersonalDest();
    void refreshI2pStatus();
    // A failure is also reported as a failed action.
    void runOp(const QString& id, const QString& kind, const QString& title,
        const QString& status, const std::function<QString()>& work);
    void emitAliasHoldings();
    void refreshContacts();
    void publishThisDeviceAddress();
    void publishFreshAddress();
    // While the Account page is open one held request keeps its overview current.
    void watchOverview(bool open);
    void revokeDevice(const QString& deviceId);
    void reviewDevices(const QStringList& revoke);
    void readContactDevices(const QString& peer);
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
    void pairOfferReady(const QString& uri, const QString& code);
    void pairStage(const QString& status, double progress);
    void pairFinished(bool ok, const QString& status);
    void accountClosed(bool ok, const QString& error);
    void accountSettings(bool acceptCalls, bool sendReceipts, bool showPresence);
    void peerPresence(const QString& peer, bool online, bool typing);
    void aliasHoldings(const QVariantList& rows, const QString& note);
    void routingTold(const QString& peer, bool delivered);
    void aliasActivationDone(bool ok, const QString& text);
    void opened(const QString& fingerprint, const QString& displayName, bool connected);
    void renamed(const QString& newName);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& connectionNote);
    void connectProgress(int percent, const QString& phase);
    void messageReceived(const bazarish::client::IncomingMessage& message);
    void contactsRefreshed(const QVector<ContactState>& contacts,
        const QStringList& blocked);
    void avatarReady(const QString& fingerprint, const QByteArray& data);
    void sendProgress(qint64 localId, int state);
    void sendResult(qint64 localId, bool ok, const QString& error);
    void sendPhase(qint64 localId, const QString& phase);
    void servedProgress(const QString& peer, const QString& e2eId, qint64 sent, qint64 total);
    void transferStage(const QString& peer, const QString& e2eId, const QString& stage);
    void servedFinished(const QString& peer, const QString& e2eId, bool ok,
        const QString& error);
    void downloadFinished(qint64 token, bool ok, const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void botActionDone(const QString& opId, bool ok, const QString& error);
    void contactRequestSent(const QString& fingerprint, const QString& intro,
        const QString& requestId, qint64 sentAtMs);
    void contactAddStage(const QString& opId, const QString& status);
    void contactAddRateLimited(
        const QString& opId, const QString& fingerprint, const QString& requestId);
    void contactAddDone(const QString& opId, bool ok, const QString& status);
    void contactAliasUnknown(const QString& opId, const QString& status);
    void contactAddResolved(const QString& opId, const QString& fingerprint);
    void contactAlreadyKnown(const QString& opId, const QString& fingerprint);
    void contactAddResumed(
        const QString& opId, const QString& uri, const QString& intro, const QString& requestId);
    void contactAccepted(const QString& peer, bool ok, const QString& reason);
    // Exactly one of the two is empty.
    void inviteState(const QString& uri, const QString& problem);
    void loginSigned(const QString& blob);
    void connectionLogReady(const QVariantList& lines);
    void connectionLogLine(const QVariantMap& line);
    void stopped();
    void syncReachable(
        bool ok, const QString& gatewayRefusal, bazarish::client::LinkFailure failure);
    void linksRebuilding();
    // shown: the backlog is more than one pass of items.
    void syncProgress(bool syncing, bool shown, qint64 takenBytes, qint64 totalBytes);
    void approvalState(bool pending, bool refused, const QString& note);
    void facadeInfo(const QString& activeAddress, const QStringList& configured,
        const QString& serverFp, const QStringList& reseeds);
    void i2pKeyState(bool hasKey, const QString& address);
    void destChangeSettled();
    // fresh: the server answered it now; otherwise it is the one kept from before.
    void overviewReady(const QVariantMap& overview, bool fresh);
    void overviewFailed();
    void mailboxUsage(qulonglong usedBytes, qulonglong quotaBytes);
    void deviceState(const QString& deviceId, bool primary, const QVariantList& ownDevices,
        qint64 epochReserveUntil, bool epochReserveLow);
    void contactDevices(const QString& peer, const QVariantList& devices);
    void callStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, bool peerRinging, qint64 connectedAtMs);
    void callLevels(float inputLevel, float outputLevel);
    void callLogged(const QString& peer, bool incoming, int outcome);
    void callEndFailed(bool declining, const QString& reason);

private:
    QString beginOp(const QString& title, const QString& status);
    static QString milestoneText(bazarish::client::ConnectMilestone milestone);
    void reportPairing(const bazarish::client::Session::PairingEvent& event);
    int opSeq_ = 0;
    void startMaintenance();
    void startReceiving();
    void emitFacadeInfo();
    void emitInvite();
    QString lastInvite_;
    QString lastInviteProblem_;
    void emitContacts();
    Q_INVOKABLE void emitDeviceState();
    QVariantList toldDeviceState_;
    void armDayTimer();
    QTimer* dayTimer_ = nullptr;
    void emitCallState();
    void reconcileCallTimer();
    struct ToldCall {
        int state = 0;
        QString peer;
        QString callId;
        bool muted = false;
        QString stage;
        bool peerRinging = false;
        qint64 connectedAtMs = 0;

        friend bool operator==(const ToldCall&, const ToldCall&) = default;
    };
    std::optional<ToldCall> toldCall_;
    std::pair<float, float> toldLevels_{0.0F, 0.0F};
    void flushCallLog();
    void startContactAdd(bool byAlias, const QString& uriOrAlias, const QString& intro,
        const QString& opId, const QString& requestId = {});
    void resumePendingAdds();
    bool addsResumed_ = false;
    bazarish::client::DeliveryWatch botActionWatch(const QString& opId);
    bazarish::client::DeliveryWatch contactRequestWatch(const QString& opId,
        const QString& fingerprint, const QString& intro, const QString& requestId,
        bool recordedHere);
    void finishContactRequest(const QString& opId, const QString& fingerprint,
        const QString& intro, const QString& requestId, bool recordedHere, qint64 sentAtMs,
        const bazarish::client::OutboundCourier::Outcome& outcome);
    void drainMailbox();
    void maintain();
    void stopMaintenance();
    void armSchedule();
    std::optional<qint64> transientCheckAtMs(qint64 nowMs) const;
    std::optional<qint64> aliasAttemptAtMs() const;
    void serviceAliasesIfDue();
    Q_INVOKABLE void takeCourierNews();
    void refreshCalls();
    Q_INVOKABLE void drainResolvedAdds();
    void startAliasErrand(bool byHand);
    Q_INVOKABLE void drainAliasErrands();
    std::unique_ptr<bazarish::client::Session> session_;
    std::mutex sessionLife_;
    std::shared_ptr<ResolvedContactAddQueue> resolvedAdds_;
    std::shared_ptr<AliasErrandQueue> aliasErrands_;
    bool aliasErrandRunning_ = false;
    bool aliasErrandByHand_ = false;
    std::optional<std::pair<QVector<ContactState>, QStringList>> toldContacts_;
    QTimer* maintenanceTimer_ = nullptr;
    bool maintaining_ = false;
    bool sessionDutiesFailed_ = false;
    QTimer* callTimer_ = nullptr;
    std::thread eventWaiter_;
    bazarish::client::MailWaitGate mailGate_;
    std::shared_ptr<std::atomic<bool>> eventWaiterRunning_;
    void startEventWaiter();
    void streamMailbox(bazarish::client::Client& waiter,
        const std::vector<bazarish::client::PendingEntry>& waiting, bool catchUp,
        const std::atomic<bool>& running);
    void beginStream(bool catchUp, std::uint64_t totalBytes, std::size_t items);
    void registerThisDevice();
    void stopEventWaiter();
    std::unique_ptr<bazarish::client::OverviewWatch> overview_;
    bool overviewOpen_ = false;
    void stopOverview();
    Q_INVOKABLE void releaseOverviewHold();
    void emitKeptOverview();
    void takeMailboxUsage(const bazarish::client::MailboxUsage& usage);
    std::optional<bazarish::client::MailboxUsage> toldUsage_;
    std::thread approvalWaiter_;
    bazarish::client::MailWaitGate approvalGate_;
    std::shared_ptr<std::atomic<bool>> approvalWaiterRunning_;
    void startApprovalWaiter();
    void stopApprovalWaiter();
    void approvalAnswered(const bazarish::client::I2pDestStatus& status);
    void approvalRefused(const std::string& reason);
    std::thread errands_;
    std::mutex errandMutex_;
    std::condition_variable errandWake_;
    struct Errand {
        std::string pendingId;
        bool releaseOverview = false;
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
    std::deque<bazarish::client::Session::MailboxItem> streamed_;
    std::vector<std::string> owedAcks_;
    bool streamEnded_ = true;
    bool catchingUp_ = false;
    bool syncShown_ = false;
    std::uint64_t streamBytes_ = 0;
    std::uint64_t takenBytes_ = 0;
    qint64 lastTransientCheckMs_ = 0;
    qint64 lastAliasServiceMs_ = 0;
};

}  // namespace bazarish::app
