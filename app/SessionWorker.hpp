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


// One contact as the account knows it. The worker ships these whole, so the
// values describing the same person cannot fall out of step with one another.
struct ContactState {
    // Where a contact request stands. Three states, not two: the middle one
    // draws a button to press, the last one the same button saying what it is
    // doing.
    enum Request { eAnswered, eWaiting, eAccepting };

    QString fingerprint;
    QString name;
    // Their invite, to pass on. Empty until they have handed over a card - which
    // is a different thing from having refused to be passed on.
    QString invite;
    bool writable = false;
    bool sharingRefused = false;
    bool notifications = true;
    bool calls = true;
    Request request = eAnswered;
};

// Holds contact-card resolutions produced off the worker thread - the slow
// federated fetch of a contact add - drained on the worker thread. Shared by
// shared_ptr with each resolve, so it outlives the worker if one is still in
// flight at teardown. Defined in the .cpp.
struct ResolvedContactAddQueue;
struct AliasErrandQueue;

// Runs all blocking Session work (open, register, send with retries, sync) on
// a dedicated thread so the UI never freezes. Lives on that worker thread;
// commands arrive via queued calls and results leave via queued signals.
class SessionWorker : public QObject {
    Q_OBJECT
public:
    ~SessionWorker() override;

private:
    // Runs one action against the open account and reports a failure the way the
    // window shows it. There is nothing to run against when no account is open,
    // which is the normal state between closing one and opening the next.
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
    // startOnline false opens the account and touches the network for nothing:
    // no sync, no long poll, no add resumed. An account the user turned off is
    // opened to be read.
    void openAccount(const QString& dir, const QString& passphrase, bool startOnline);
    // Drops the record of an add that has ended.
    void forgetPendingAdd(const QString& opId);
    void connectAndRegister(const QStringList& facadeUrls, const QString& serverFp,
        const QStringList& reseedUrls);
    // Reads the mailbox once. Called when the mail loop says something is
    // waiting, and at the few moments the account itself asks for a look (it has
    // just come online, or just agreed to a contact).
    void sync();
    // Starts or stops background syncing (the account going online/offline).
    void setSyncEnabled(bool on);
    void rebuildI2pLinks();
    void sendText(const QString& peer, const QString& text, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void sendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& e2eId, const QString& replyTo);
    void sendPicture(const QString& peer, const QString& localPath, qint64 localId,
        const QString& e2eId, const QString& replyTo);
    void sendVoice(const QString& peer, const QByteArray& opus, qint64 durationMs, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void sendReceipt(const QString& peer, const QString& refId);
    // Acks a pending mailbox item (deferred ack): called by the controller after it
    // has durably stored the item, so the server only drops it once it is safe.
    void ackPending(const QString& pendingId);
    // Sets our reaction emoji on a message; empty emoji removes it.
    void sendReaction(const QString& peer, const QString& refId, const QString& emoji);
    void sendCallback(
        const QString& opId, const QString& peer, const QString& data, const QString& ref);
    void sendCommand(
        const QString& opId, const QString& peer, const QString& command, const QString& args);
    void sendEdit(const QString& peer, const QString& refId, qint64 localId, const QString& text);
    void sendDelete(const QString& peer, const QString& refId);
    // Forget a file this device announced, telling the peer nothing: a message
    // deleted only here must not leave the record that would still serve it.
    void dropSentFile(const QString& refId);
    // Compresses the picked image to a square avatar within the protocol cap and
    // sets it (persist + distribute to contacts and the account's other devices).
    void setAvatar(const QString& localPath);
    void clearAvatar();
    // Changes the account's own display name (local + future invites only).
    void setDisplayName(const QString& name);
    // Renames a contact locally (mirrored only to the account's other devices).
    void renameContact(const QString& peer, const QString& name);
    // Permanently removes a contact (local + irreversible); the avatar store is
    // cleared and the contact list is re-emitted.
    void removeContact(const QString& peer);
    void clearSaved();
    void setBlocked(const QString& peer, bool blocked);
    void setContactNotifications(const QString& peer, bool on);
    void setContactCalls(const QString& peer, bool allowed);
    void syncChatPin(const QString& peer, bool pinned);
    void syncRead(const QString& peer, qint64 sentAtMs);
    void syncChatClear(const QString& peer);
    // Re-publishes the account-wide answers, for a change that arrived from
    // another device of ours.
    void emitSettings();
    // Asks the peer to clear the whole conversation with us (chat.clear); their
    // client wipes its transcript on receipt.
    void clearChatForEveryone(const QString& peer);
    void addByInvite(const QString& uri, const QString& intro, const QString& opId,
        const QString& requestId);
    void addByAlias(const QString& alias, const QString& intro, const QString& opId);
    // Agrees to a received contact request (sends our descriptor back).
    void acceptContact(const QString& peer);
    void requestInvite();
    // Signs a portal/third-party login challenge with this account's key. Local
    // only - no server is contacted - so it works before a server is connected.
    void signLogin(const QString& challenge);
    void connectionLog();
    void clearConnectionLog();
    void askDevicesForContacts();
    // The end of a command. Connected behind every one of them, so it runs on
    // this thread once the command it follows has returned - which is how the
    // controller knows a command is over without each one saying so.
    void noteCommandDone();
    // Stops everything this worker owns and closes the account. Answered by
    // stopped(); the thread's loop is ended by the controller after that.
    void shutdown();
    void saveAttachment(const QString& peer, const QString& e2eId, const QString& destPath, qint64 token);
    void exportAccount(const QString& path, const QString& password);
    void changePassphrase(const QString& passphrase);
    void rotateServingKey();
    void activateAliasServicing();
    void setSharingAllowed(bool allowed);
    // Per-user I2P destination: set up the master (generate or load a .dat),
    // turn the paid option on/off, and report the current status.
    void generatePersonalKey();
    void loadPersonalKey(const QString& path);
    void deletePersonalKey();
    void setAcceptCalls(bool accept);
    void setSendReceipts(bool on);
    void setDelegationDays(int days);
    void cancelTransfer(const QString& e2eId);
    void publishPersonalDest();
    void disablePersonalDest();
    void refreshI2pStatus();
    // Re-emits the contact list. Called by name from a delivery outcome, which
    // runs on the courier's thread: what a send spent has to reach the window.
    void refreshContacts();
    // The two answers to "your server serves an address this device has no keys
    // for": keep this device's own address, or start from a fresh one.
    void publishThisDeviceAddress();
    void publishFreshAddress();
    // Polls the user's own storage usage (mailbox + blob backends) and reports it.
    void refreshStorageUsage();
    // The devices registered on this account, and dropping one.
    void refreshDevices();
    void forgetDevice(const QString& clientId);
    // Ends the account on its server. Reported through accountClosed, because
    // the caller has to know whether it happened before it deletes the profile
    // that holds the only key able to ask again.
    void closeAccountOnServer();
    // Calls: each runs the matching Session method (strict I2P, so a failure
    // surfaces as actionFailed) and then re-emits the call state.
    void startCall(const QString& peer);
    void acceptCall(const QString& callId);
    void declineCall(const QString& callId);
    void endCall();
    void setCallMuted(bool muted);

signals:
    void commandFinished();
    // The server serves an address no device of this account answered for. The
    // window puts the choice to the user; nothing is published until it does.
    void addressMismatch(const QString& servedHost, const QString& ourHost);
    // Handed to the controller when the account opens, so a login is signed on
    // the thread the user clicked on rather than behind this worker's queue.
    void loginSignerReady(std::shared_ptr<bazarish::client::LoginSigner> signer);
    // Every observable worker operation opens with opBegin and closes with opDone,
    // so the activity panel shows one row per operation. Sends and calls keep their
    // own richer rows.
    void opBegin(const QString& opId, const QString& kind, const QString& title,
        const QString& status);
    void opDone(const QString& opId, bool ok, const QString& status);
    // A row's status line changed while it is still running.
    void opProgress(const QString& opId, const QString& status);
    // The account was ended on its server, or the attempt failed with this
    // reason. Nothing may be deleted locally until this says it happened.
    void accountClosed(bool ok, const QString& error);
    // What the opened account has stored for the settings the window shows.
    void accountSettings(bool acceptCalls, bool sendReceipts, bool sharingAllowed);
    // One step of a serving-key rotation, as it happens.
    void servingKeyStage(const QString& stage);
    // The rotation finished: ok with the summary, or the reason it did not.
    void servingKeyDone(bool ok, const QString& text);
    // What the name service says this account holds, and how it went: a row per
    // alias for the table, and the one line said under it (or nothing).
    void aliasHoldings(const QVariantList& rows, const QString& note);
    void aliasActivationDone(bool ok, const QString& text);
    void opened(const QString& fingerprint, const QString& displayName, bool connected);
    // The account's own display name was changed (so the GUI updates it without a
    // full re-open).
    void renamed(const QString& newName);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& connectionNote);
    // Coarse progress while connectAndRegister runs: it is several network round
    // trips and, over I2P, minutes - the connect screen must see it move.
    void connectProgress(int percent, const QString& phase);
    void messageReceived(const QVariantMap& message);
    // Every contact this account holds, and the fingerprints it has blocked. The
    // blocked are separate because one of them need not be a contact.
    void contactsRefreshed(const QVector<ContactState>& contacts,
        const QStringList& blocked);
    // A real avatar became available for an identity (own or a contact): the GUI
    // feeds it to the shared avatar store. Empty data clears it.
    void avatarReady(const QString& fingerprint, const QByteArray& data);
    void sendProgress(qint64 localId, int state);
    void sendResult(qint64 localId, bool ok, const QString& error);
    // Where a send has got to (preparing / dialing / sending / retry n of m), so
    // the activity panel and a retrying bubble show real progress.
    void sendPhase(qint64 localId, const QString& phase);
    // Upload progress for an outgoing file (bytes sent so far, total bytes).
    void uploadProgress(qint64 localId, qint64 sent, qint64 total);
    // Download progress for an incoming attachment being saved (token = message
    // id): received/total ciphertext bytes.
    void downloadProgress(qint64 token, qint64 received, qint64 total);
    // Bytes leaving this device for a file we are serving, by the announced file
    // id (the outgoing message's protocol id): the sender watches the transfer in
    // the bubble it sent, not in a panel somewhere else.
    void servedProgress(const QString& peer, const QString& e2eId, qint64 sent, qint64 total);
    // What the transfer is doing before (and between) bytes, for the bubble.
    void transferStage(const QString& peer, const QString& e2eId, const QString& stage);
    void servedFinished(const QString& peer, const QString& e2eId, bool ok,
        const QString& error);
    // An attachment download/save finished (token identifies the message): ok is
    // false with an error string on failure.
    void downloadFinished(qint64 token, bool ok, const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    // A button press has left (or has not): the activity panel's row is closed
    // by this, whichever way it went.
    void botActionDone(const QString& opId, bool ok, const QString& error);
    // A contact request went out: who it reached, the intro it carried, and the
    // name it goes by on the wire. A resend carries the same name, so the note in
    // the chat stays the same note.
    void contactRequestSent(
        const QString& fingerprint, const QString& intro, const QString& requestId);
    // Activity-panel progress for an in-flight contact add (opId assigned by the
    // controller at start): a stage update, then a terminal done (ok + final text).
    void contactAddStage(const QString& opId, const QString& status);
    // The recipient's address is over its contact-request cap: the request was
    // refused, not lost, and the controller repeats it on a timer.
    void contactAddRateLimited(
        const QString& opId, const QString& fingerprint, const QString& requestId);
    void contactAddDone(const QString& opId, bool ok, const QString& status);
    // The add resolved to somebody already in the book: nothing was sent, and the
    // chat with them is what the user was after.
    void contactAlreadyKnown(const QString& opId, const QString& fingerprint);
    // A contact request we agreed to: the peer, and whether it went through.
    void contactAccepted(const QString& peer, bool ok, const QString& reason);
    void inviteReady(const QString& uri);
    void inviteUnavailable(const QString& reason);
    // The signed login blob for a challenge (sign-in-with-key result).
    void loginSigned(const QString& blob);
    void connectionLogReady(const QVariantList& lines);
    // Everything this worker was running has stopped and the account is closed.
    void stopped();
    // Whether the last sync reached the facade (true) or failed (false).
    // reason carries why a failed sync failed, so an account stuck at
    // "Connecting" can say what is wrong instead of only that it is not right.
    void syncReachable(bool ok, const QString& reason);
    // Whether the serving server is still holding this account for an operator to
    // approve, and what that operator has to say about it. A moderated server
    // takes the account, answers every request and serves none of it, so this is
    // the only thing that tells the two apart.
    void approvalState(bool pending, const QString& note);
    // The facade currently in use, the configured facade list, the server
    // fingerprint and the reseeds the endpoint carries, for the GUI.
    void facadeInfo(const QString& activeUrl, const QStringList& configured,
        const QString& serverFp, const QStringList& reseeds);
    // hasKey: a master is set up in the account. delegated: the server holds a
    // delegation for it. live: delegated and the account is approved, so the
    // destination is being served. address: the b32 (empty if none). summary: a
    // one-line human status for the settings page. transientExpires: when the
    // current delegation lapses (0 when there is none).
    void i2pStatus(bool hasKey, bool delegated, bool live, const QString& address,
        const QString& summary, qint64 transientExpires, const QString& serverState);
    // The half that needs no server: whether this account holds a destination key
    // and at what address. Emitted as soon as it is known, so the view never waits
    // on a server poll to say whether a key exists at all.
    void i2pKeyState(bool hasKey, const QString& address);
    // The address the server says it serves for this account.
    void i2pServedAddress(const QString& address);
    // This account's registered devices: {clientId, current}. A message is kept
    // until every one of them has acked it, so a device nobody uses any more
    // holds mail until the retention window ends.
    void devicesReady(const QVariantList& devices);
    // The user's storage usage (mailbox + blob), each with an `ok` flag (a backend
    // that did not answer keeps its last figures and is marked stale by the UI).
    void storageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota);
    // The serving server's onboarding info, shown when a connect is
    // refused because this key is not registered: the refusal reason, the
    // server's message and its registration link(s).
    void serverHello(const QString& reason, const QString& message, const QStringList& links);
    // Call lifecycle: state is 0 idle / 1 outgoing / 2 incoming / 3 active,
    // matching Session::CallState. Emitted after every sync and call action.
    void callStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, bool peerRinging, qint64 connectedAtMs, float inputLevel,
        float outputLevel);
    // A call finished: its peer, direction (incoming), how it ended (a
    // Session::CallOutcome as an int) and connected duration - for a chat-history
    // entry. Emitted after sync and after any call action.
    void callLogged(const QString& peer, bool incoming, int outcome, qint64 durationSec);

private:
    // Opens a background-activity row for a worker operation and returns its id;
    // pair it with emit opDone(id, ok, status). One uniform helper so every
    // observable operation is surfaced the same way.
    QString beginOp(const QString& kind, const QString& title, const QString& status);
    int opSeq_ = 0;
    void startReceiving();
    void emitFacadeInfo();
    // Emits the current contacts with their display names (parallel lists).
    void emitContacts();
    void emitCallState();
    // Runs the call watch while a call is live and stops it once idle.
    void reconcileCallTimer();
    // Drains finished calls from the session and emits callLogged for each.
    void flushCallLog();
    // Starts an asynchronous contact add: snapshots the transport context on this
    // thread, then runs the slow federated card fetch on a detached background
    // thread (its own transport) so sync and the connection are never blocked. The
    // result is drained and finalized by drainResolvedAdds on a later sync tick.
    void startContactAdd(bool byAlias, const QString& uriOrAlias, const QString& intro,
        const QString& opId, const QString& requestId = {});
    // Takes up every add the last run did not finish. Called once, at open.
    void resumePendingAdds();
    // Reads the mailbox: one bounded pass over what is waiting, surfacing each
    // item to the GUI. Called when the mail loop says something is there, never
    // on a timer.
    void drainMailbox();
    // The periodic local upkeep: adds resolved off-thread, call timeouts, and -
    // each on its own longer guard - the approval and delegation checks. Reads
    // no mail.
    void maintain();
    // Advances call timeouts and publishes the resulting state.
    void refreshCalls();
    // Finalizes any off-thread contact-card resolutions that have completed:
    // commits the add and emits the result. Run each sync.
    void drainResolvedAdds();
    // Starts the alias errand on a thread of its own - it is two or more I2P
    // round trips and each has minutes of budget, which is not something to hold
    // the worker thread for. byHand says whether a person pressed the button, so
    // a run started by the upkeep tick reports to nobody.
    void startAliasErrand(bool byHand);
    // Applies whatever the errand brought back and says how it went. Run each
    // upkeep tick.
    void drainAliasErrands();
    std::unique_ptr<bazarish::client::Session> session_;
    // Completed off-thread contact resolutions awaiting finalize (see above).
    std::shared_ptr<ResolvedContactAddQueue> resolvedAdds_;
    // Completed off-thread alias errands awaiting apply (see above).
    std::shared_ptr<AliasErrandQueue> aliasErrands_;
    // One errand at a time: the button and the upkeep tick both start one.
    bool aliasErrandRunning_ = false;
    // Whether anybody is waiting to be told how the running errand went. A press
    // that lands while the tick's own errand is in the air adopts it rather than
    // starting a second - and then somebody is waiting, so this turns on.
    bool aliasErrandByHand_ = false;
    // Local upkeep only. Mail is not on it: the mailbox is read when the wait
    // below says something is there, and at no other time.
    QTimer* maintenanceTimer_ = nullptr;
    // While a call is up, its state is watched far faster than the upkeep tick:
    // both sides say "in call" the moment media flows, and neither can learn
    // that seconds late.
    QTimer* callTimer_ = nullptr;
    // The mail loop: its own thread, because the request is meant to hang. It is
    // the only way this client hears about incoming mail - a wait that fails is
    // asked again at once, because there is nothing else to fall back to.
    std::thread eventWaiter_;
    std::shared_ptr<std::atomic<bool>> eventWaiterRunning_;
    void startEventWaiter();
    void stopEventWaiter();
    // What a user never waits for and what costs a round trip each: giving a
    // mailbox item back once the interface has stored it. A pass hands back one
    // per item, and every one of them used to sit in this thread's queue - so a
    // message written just after mail arrived waited out five round trips before
    // its own command was even reached. They are made here instead; the API
    // client serializes its own requests, which is what makes that safe.
    std::thread errands_;
    std::mutex errandMutex_;
    std::condition_variable errandWake_;
    // One small request the user is not waiting for: handing a mailbox item
    // back, or putting the copy of a sent message where this account's other
    // devices will find it.
    struct Errand {
        std::string pendingId;   // an ack when this is set
        std::string deliveryId;  // an envelope for our own devices when this is
        bazarish::Bytes sealed;
        std::string kind;
    };
    std::deque<Errand> errandQueue_;
    bool errandsRunning_ = false;
    void startErrands();
    void stopErrands();
    void queueErrand(Errand errand);
    // Hands one item back to the server, later and elsewhere. Both ack paths
    // come here: the one the interface asks for once it has stored a message,
    // and the one a mailbox pass decides on for what it consumed itself.
    void queueAck(const std::string& pendingId);

    // The handshake between that loop and this worker. A mailbox holds an item
    // until it is acked, so it answers the next wait the instant one is asked
    // for - and the loop would spin through the drain. It therefore waits here
    // until the pass it woke has taken all it can and given back what it holds.
    void settleDrain();
    std::mutex drainMutex_;
    std::condition_variable drainDone_;
    bool drainSettled_ = true;
    // When the delegation renewal was last considered (never = 0).
    qint64 lastTransientCheckMs_ = 0;
    qint64 lastAliasServiceMs_ = 0;
    qint64 lastApprovalCheckMs_ = 0;
    // When this device last tried to register itself with the server (never = 0).
    qint64 lastRegisterAttemptMs_ = 0;
    // Set true to abort in-flight downloads (teardown / session switch); the fetch
    // polls it to close a parked read and stop retrying, and queued tasks skip
    // emitting onto a tearing-down session.
    std::atomic<bool> downloadsCancelled_{false};
    // Attachment downloads run here, off the worker thread, so a long or stalled
    // fetch never blocks sends, uploads or sync. Declared last so it is drained
    // before session_ is destroyed; its tasks capture session_ and the cancel flag.
    QThreadPool downloadPool_;
};

}  // namespace bazarish::app
