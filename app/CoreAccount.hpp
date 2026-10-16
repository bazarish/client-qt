// Bazarish project (c) 2026
#pragma once

#include "AccountCommands.hpp"
#include "CallTones.hpp"
#include "AccountEvents.hpp"
#include "ContactBook.hpp"
#include "MessageIngest.hpp"
#include "OperationLog.hpp"
#include "TranscriptStore.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <memory>

namespace bazarish::client {
class AccountDb;
class LoginSigner;
}

namespace bazarish::app {

class SessionWorker;

class CoreAccount : public QObject {
    Q_OBJECT
public:
    explicit CoreAccount(const QString& accountId, QObject* parent = nullptr);
    ~CoreAccount() override;

    CoreAccount(const CoreAccount&) = delete;
    CoreAccount& operator=(const CoreAccount&) = delete;

    AccountCommands& commands() { return commands_; }
    AccountEvents& events() { return events_; }
    QString accountId() const { return accountId_; }
    QString fingerprint() const { return contacts_.ownFingerprint(); }
    QString displayName() const { return displayName_; }
    bool online() const { return online_; }
    bool reachable() const { return reachable_; }
    QString activeFacade() const { return activeFacade_; }
    bool hasServer() const { return hasServer_; }
    QString callState() const { return call_.state; }
    QString callPeer() const { return call_.peer; }
    QString callPeerName() const { return contacts_.peerName(call_.peer); }
    int unreadTotal() const { return unread_; }

    void open(const QString& file, const QString& passphrase, bool startOnline);
    void goOnline();
    void goOffline();
    void rebuildI2pLinks();
    void beginShutdown();
    void shutdown();

signals:
    void changed();
    void callChanged();
    void messageNotification(const QString& peer, const QString& fromName);
    void reactionNotification(const QString& peer, const QString& fromName, const QString& emoji);

private slots:
    void noteCommandQueued();

private:
    void onCommandFinished();
    void showSlowCommands();
    void close();
    void onSyncReachable(bool ok);
    void announcePresence();
    void recountUnread();
    void signLogin(const QString& challenge);
    void onMessageReceived(const QVariantMap& message);
    void viewConversation(const QString& peer);
    void markRead(const QString& peer, qint64 id, const QString& e2eId, qint64 sentAtMs);
    void flushReadSync();
    void noteReactionToFlash(const QString& peer, const QString& target);
    void forgetReactionFlash(const QString& peer);
    void persistReactionsToFlash();
    void reportUnsaved();
    bool isSavedChat(const QString& peer) const;
    void sendText(
        const QString& peer, const QString& text, const QString& replyTo, const QString& e2eId);
    qint64 beginAttachmentSend(StoredMessage m);
    void sendFile(
        const QString& peer, const QString& path, const QString& replyTo, const QString& e2eId);
    void sendPicture(const QString& peer, const QByteArray& bytes, const QString& name,
        const QString& mime, const QString& replyTo, const QString& e2eId);
    void sendVoice(const QString& peer, const QByteArray& opus, qint64 durationMs,
        const QString& wave, const QString& replyTo, const QString& e2eId);
    void forwardMessage(const QString& fromPeer, const QString& e2eId, const QString& toPeer,
        const QString& newE2eId);
    void resend(const QString& peer, qint64 localId);
    void editMessage(
        const QString& peer, qint64 localId, const QString& e2eId, const QString& text);
    void deleteMessage(const QString& peer, qint64 localId, const QString& e2eId, bool outgoing);
    void react(const QString& peer, const QString& e2eId, const QString& emoji);
    void rememberReaction(const QString& emoji);
    void pinChat(const QString& peer, bool pinned);
    void clearChat(const QString& peer, bool forEveryone);
    void forgetChat(const QString& peer);
    void clearSavedEverywhere();
    void addByInvite(const QString& uri, const QString& intro);
    void addByAlias(const QString& alias, const QString& intro);
    void retryContactAdd(const QString& peer);
    void retryContactRequest(const QString& fingerprint);
    void sendContactAdd(const QString& uri, const QString& intro, const QString& requestId);
    void onContactAddResumed(
        const QString& opId, const QString& uri, const QString& intro, const QString& requestId);
    void trackContactAdd(
        const QString& opId, const QString& uri, const QString& intro, const QString& requestId);
    void onContactAddRateLimited(
        const QString& opId, const QString& fingerprint, const QString& requestId);
    void syncAgreeingRows();
    struct PendingContactRequest {
        QString uri;
        QString intro;
        int triesLeft = 0;
        QString requestId;
    };
    void repeatContactAdd(const QString& chat, const PendingContactRequest& pending);
    void startAliasAdd(const QString& alias, const QString& intro, const QString& requestId,
        const QString& chat);
    void trackAliasAdd(const QString& opId, const QString& alias, const QString& intro,
        const QString& requestId, const QString& chat);
    void writeConversationNote(const QString& peer, const QString& text);
    void onRoutingTold(const QString& peer, bool delivered);
    void openContactProgress(const QString& peer, const QString& opId);
    void noteContactProgress(const QString& peer, const QString& opId);
    void writeContactProgress(const QString& opId, const QString& text);
    void onContactAddStage(const QString& opId, const QString& status);
    void onContactAddDone(const QString& opId, bool ok, const QString& status);
    void onContactRequestUnconfirmed(const QString& opId);
    void settleContactAdd(const QString& opId, bool ok, const QString& note);
    void onContactAlreadyKnown(const QString& opId, const QString& fingerprint);
    void onContactAliasUnknown(const QString& opId, const QString& status);
    void onContactAddResolved(const QString& opId, const QString& fingerprint);
    void onContactRequestSent(const QString& fingerprint, const QString& intro,
        const QString& requestId, qint64 sentAtMs);
    void onCallStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, bool peerRinging, qint64 connectedAtMs, float inputLevel,
        float outputLevel);
    void onCallLogged(const QString& peer, bool incoming, int outcome, qint64 durationSec);
    void declineCall();
    void endCall();
    void onConnectRequested();
    void onConnectProgress(int percent, const QString& phase);
    void settleConnect(bool connected, const QString& note);
    void sendCallback(
        const QString& peer, const QString& data, const QString& ref, const QString& label);
    void sendCommand(
        const QString& peer, const QString& command, const QString& args, const QString& label);
    void saveAttachment(
        const QString& peer, const QString& e2eId, const QString& dest, qint64 token);
    void onUploadProgress(qint64 localId, qint64 sent, qint64 total);
    void onDownloadProgress(qint64 token, qint64 received, qint64 total);
    void onTransferStage(const QString& peer, const QString& e2eId, const QString& stage);
    void onServedProgress(const QString& peer, const QString& e2eId, qint64 sent, qint64 total);
    void onServedFinished(const QString& peer, const QString& e2eId, bool ok, const QString& error);
    void onDownloadFinished(qint64 token, bool ok, const QString& error);
    void compact();
    void trim(const QString& peer, int keep);
    void restartDelivery(const QString& peer, qint64 localId);
    void bumpStatus(qint64 localId, int status);
    void onSendProgress(qint64 localId, int state);
    void onSendResult(qint64 localId, bool ok, const QString& error);
    void onSendPhase(qint64 localId, const QString& phase);
    void noteDeliveredThrough(const QString& peer, qint64 uptoId);
    client::AccountDb& accountDb();

    QString accountId_;
    AccountCommands commands_;
    AccountEvents events_;
    QString displayName_;
    QString activeFacade_;
    bool hasServer_ = false;
    bool reachable_ = false;
    int unread_ = 0;
    bool linksRebuilding_ = false;
    QTimer rebuildGrace_;
    QThread thread_;
    SessionWorker* worker_ = nullptr;
    std::shared_ptr<client::LoginSigner> loginSigner_;
    TranscriptStore store_;
    ContactBook contacts_;
    MessageIngest ingest_{store_, contacts_};
    QString accountPath_;
    QString passphrase_;
    std::unique_ptr<client::AccountDb> accountDb_;
    QStringList reactionsToFlash_;
    QStringList recentReactions_;
    QString viewedPeer_;
    bool startOnline_ = true;
    bool online_ = false;
    bool sendReceipts_ = true;
    QHash<QString, qint64> lastReadAckedId_;
    QHash<QString, qint64> pendingReadSync_;
    QTimer readSyncTimer_;
    OperationLog log_;
    QHash<qint64, int> statusById_;
    QSet<qint64> savedSends_;
    struct ContactProgressRow {
        qint64 id = 0;
        QString peer;
    };
    QHash<QString, ContactProgressRow> contactProgressRows_;
    QHash<QString, QString> aliasChats_;
    QHash<QString, PendingContactRequest> refusedRequests_;
    QSet<QString> agreeingShown_;
    struct CallView {
        QString state = QStringLiteral("idle");
        QString peer;
        QString id;
        QString stage;
        qint64 connectedAtMs = 0;
    };
    CallView call_;
    bool connecting_ = false;
    int connectPercent_ = 0;
    QHash<qint64, QString> pendingSavePath_;
    QHash<QString, QString> transferStages_;
    QString refusedCallId_;
    QString callOpId_;
    bool callEndedLocally_ = false;
    CallTones callTones_;
    struct QueuedCommand {
        QString id;
        QString title;
        qint64 queuedAtMs = 0;
        bool shown = false;
    };
    QList<QueuedCommand> commandQueue_;
    qint64 commandSeq_ = 0;
    QTimer commandTimer_;
};

}  // namespace bazarish::app
