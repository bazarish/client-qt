// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "Session.hpp"
#include "ProfileDb.hpp"
#include "TranscriptStore.hpp"
#include "VoiceNote.hpp"

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
#include <map>
#include <memory>
#include <string>

class QTimer;

namespace bazarish::app {


// Holds contact-card resolutions produced off the worker thread (the slow
// federated fetch of a contact add), drained and finalized on the worker thread.
// Shared by shared_ptr with each background resolve so it outlives the worker if a
// resolve is still in flight at teardown. Defined in the .cpp.
struct ResolvedContactAddQueue;

// Runs all blocking Session work (open, register, send with retries, sync) on
// a dedicated thread so the UI never freezes. Lives on that worker thread;
// commands arrive via queued calls and results leave via queued signals.
class SessionWorker : public QObject {
    Q_OBJECT
public:
    ~SessionWorker() override;

public slots:
    void openProfile(const QString& dir, const QString& passphrase);
    void connectAndRegister(const QStringList& facadeUrls, const QString& serverFp);
    void sync();
    // Starts or stops background syncing (the account going online/offline).
    void setSyncEnabled(bool on);
    void rebuildI2pLinks();
    void sendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void sendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void sendPicture(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void sendVoice(const QString& peer, const QByteArray& opus, qint64 durationMs, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void sendReceipt(const QString& peer, const QString& refId);
    // Acks a pending mailbox item (deferred ack): called by the controller after it
    // has durably stored the item, so the server only drops it once it is safe.
    void ackPending(const QString& pendingId);
    // Sets our reaction emoji on a message; empty emoji removes it.
    void sendReaction(const QString& peer, const QString& refId, const QString& emoji);
    void sendCallback(const QString& peer, const QString& data, const QString& ref);
    void sendCommand(const QString& peer, const QString& command, const QString& args);
    void sendEdit(const QString& peer, const QString& refId, qint64 localId, const QString& text);
    void sendDelete(const QString& peer, const QString& refId);
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
    void syncChatPin(const QString& peer, bool pinned);
    // Asks the peer to clear the whole conversation with us (chat.clear); their
    // client wipes its transcript on receipt.
    void clearChatForEveryone(const QString& peer);
    void addByInvite(const QString& uri, const QString& intro, const QString& opId);
    void addByUsername(const QString& alias, const QString& intro, const QString& opId);
    // Agrees to a received contact request (sends our descriptor back).
    void acceptContact(const QString& peer);
    void requestInvite();
    // Signs a portal/third-party login challenge with this profile's key. Local
    // only - no server is contacted - so it works before a server is connected.
    void signLogin(const QString& challenge);
    void saveAttachment(const QString& peer, const QString& messageId, const QString& destPath, qint64 token);
    void exportProfile(const QString& path, const QString& password);
    // Per-user I2P destination: set up the master (generate or load a .dat),
    // turn the paid option on/off, and report the current status.
    void generatePersonalKey();
    void loadPersonalKey(const QString& path);
    void deletePersonalKey();
    void allowClearnet(bool allow);
    void setAcceptCalls(bool accept);
    void cancelTransfer(const QString& protocolId);
    void publishPersonalDest();
    void disablePersonalDest();
    void refreshI2pStatus();
    // Polls the user's own storage usage (mailbox + blob backends) and reports it.
    void refreshStorageUsage();
    // The devices registered on this account, and dropping one.
    void refreshDevices();
    void forgetDevice(const QString& clientId);
    // Calls: each runs the matching Session method (strict I2P, so a failure
    // surfaces as actionFailed) and then re-emits the call state.
    void startCall(const QString& peer);
    void acceptCall(const QString& callId);
    void declineCall(const QString& callId);
    void endCall();
    void setCallMuted(bool muted);
    // first openProfile so the injected backend can reach them.

signals:
    // General background-activity stream: every observable worker operation - a
    // contact request, a reaction, a read receipt, an incoming-mail pull - opens
    // with opBegin and closes with opDone, so the activity panel shows one uniform,
    // responsive row per operation.
    // (Message/file sends and calls keep their richer dedicated rows.)
    void opBegin(const QString& opId, const QString& kind, const QString& title,
        const QString& status);
    void opDone(const QString& opId, bool ok, const QString& status);
    // A row's status line changed while it is still running.
    void opProgress(const QString& opId, const QString& status);
    // What the opened profile has stored for the settings the window shows.
    void profileSettings(bool acceptCalls, bool allowClearnet);
    void opened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& connectionNote);
    // The account's own display name was changed (so the GUI updates it without a
    // full re-open).
    void renamed(const QString& newName);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& connectionNote);
    // Coarse progress while connectAndRegister runs: it is several network round
    // trips and, over I2P, minutes - the connect screen must see it move.
    void connectProgress(int percent, const QString& phase);
    void messageReceived(const QVariantMap& message);
    // The current contacts, their local display names, and per-contact "1"/"0"
    // pending flags (a contact we received a request from but have not yet
    // accepted) - all parallel lists.
    // links carries each contact's shareable descriptor, empty where none is
    // known yet - it is built from routing the session already holds.
    void contactsRefreshed(const QStringList& fingerprints, const QStringList& names,
        const QStringList& pending, const QStringList& links, const QStringList& capacities);
    // A real avatar became available for an identity (own or a contact): the GUI
    // feeds it to the shared avatar store. Empty data clears it.
    void avatarReady(const QString& fingerprint, const QByteArray& data);
    void sendProgress(qint64 localId, int state);  // 1 = accepted by own server (grey)
    void sendResult(qint64 localId, bool ok, const QString& error);
    // Our server stopped tracking the send without a delivered-ack (retries
    // exhausted / attempt forgotten): the message stays grey but its activity-panel
    // operation must settle. Carries a short note.
    void sendSettled(qint64 localId, const QString& note);
    // The server's live federation phase for a still-pending send (queued / dialing
    // / sending / awaiting-ack), so the activity panel shows real delivery progress.
    void sendPhase(qint64 localId, const QString& phase);
    // Upload progress for an outgoing file (bytes sent so far, total bytes).
    void uploadProgress(qint64 localId, qint64 sent, qint64 total);
    // Download progress for an incoming attachment being saved (token = message
    // id): received/total ciphertext bytes.
    void downloadProgress(qint64 token, qint64 received, qint64 total);
    // Bytes leaving this device for a file we are serving, by the announced file
    // id (the outgoing message's protocol id): the sender watches the transfer in
    // the bubble it sent, not in a panel somewhere else.
    void servedProgress(const QString& peer, const QString& protocolId, qint64 sent, qint64 total);
    // What the transfer is doing before (and between) bytes, for the bubble.
    void transferStage(const QString& peer, const QString& protocolId, const QString& stage);
    void servedFinished(const QString& peer, const QString& protocolId, bool ok,
        const QString& error);
    // Download stage for an incoming attachment (token = message id): the int is a
    // bazarish::client::BlobFetchStage (0 connecting, 1 downloading, 2 reconnecting),
    // so a stalled transfer reads as "reconnecting" rather than a frozen bar.
    void downloadStage(qint64 token, int stage);
    // An attachment download/save finished (token identifies the message): ok is
    // false with an error string on failure.
    void downloadFinished(qint64 token, bool ok, const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    // A contact request was sent (add-by-invite/username/fingerprint succeeded):
    // the resolved peer fingerprint and the intro text it carried, so the GUI can
    // open the chat and show the sent request straight away.
    void contactRequestSent(const QString& fingerprint, const QString& intro);
    // Activity-panel progress for an in-flight contact add (opId assigned by the
    // controller at start): a stage update, then a terminal done (ok + final text).
    void contactAddStage(const QString& opId, const QString& status);
    void contactAddDone(const QString& opId, bool ok, const QString& status);
    // A contact request we agreed to: the peer, and whether it went through.
    void contactAccepted(const QString& peer, bool ok, const QString& reason);
    void inviteReady(const QString& uri);
    void inviteUnavailable(const QString& reason);
    // The signed login blob for a challenge (sign-in-with-key result).
    void loginSigned(const QString& blob);
    // Whether the last sync reached the facade (true) or failed (false).
    // reason carries why a failed sync failed, so an account stuck at
    // "Connecting" can say what is wrong instead of only that it is not right.
    void syncReachable(bool ok, const QString& reason);
    // Whether the serving server is still holding this account for an operator to
    // approve, and what that operator has to say about it. A moderated server
    // takes the account, answers every request and serves none of it, so this is
    // the only thing that tells the two apart.
    void approvalState(bool pending, const QString& note);
    // The facade currently in use, the configured facade list, and the server
    // fingerprint, for the GUI.
    void facadeInfo(
        const QString& activeUrl, const QStringList& configured, const QString& serverFp);
    // hasKey: a master is set up in the profile. delegated: the server holds a
    // delegation for it. live: delegated and the account is approved, so the
    // destination is being served. address: the b32 (empty if none). summary: a
    // one-line human status for the settings page. transientExpires: when the
    // current delegation lapses (0 when there is none).
    void i2pStatus(bool hasKey, bool delegated, bool live, const QString& address,
        const QString& summary, qint64 transientExpires, const QString& serverState);
    // The half that needs no server: whether this profile holds a destination key
    // and at what address. Emitted as soon as it is known, so the view never waits
    // on a server poll to say whether a key exists at all.
    void i2pKeyState(bool hasKey, const QString& address);
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
        const QString& stage, qint64 connectedAtMs);
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
    void ensureSyncTimer();
    void emitFacadeInfo();
    // Emits the current contacts with their display names (parallel lists).
    void emitContacts();
    void emitCallState();
    // Drains finished calls from the session and emits callLogged for each.
    void flushCallLog();
    // Re-polls sends still in flight after their initial submit window so a late
    // delivery (yellow) or failure (red) reaches the message; run each sync.
    void reconcilePendingSends();
    // Starts an asynchronous contact add: snapshots the transport context on this
    // thread, then runs the slow federated card fetch on a detached background
    // thread (its own transport) so sync and the connection are never blocked. The
    // result is drained and finalized by drainResolvedAdds on a later sync tick.
    void startContactAdd(
        bool byUsername, const QString& uriOrAlias, const QString& intro, const QString& opId);
    // Finalizes any off-thread contact-card resolutions that have completed:
    // commits the add and emits the result. Run each sync.
    void drainResolvedAdds();
    std::unique_ptr<bazarish::client::Session> session_;
    // Completed off-thread contact resolutions awaiting finalize (see above).
    std::shared_ptr<ResolvedContactAddQueue> resolvedAdds_;
    QTimer* syncTimer_ = nullptr;
    // The long-poll loop: its own thread, because the request is meant to hang.
    // While it works the sync timer only heartbeats; if the server has no event
    // face it stops and the timer goes back to its short interval.
    std::thread eventWaiter_;
    std::shared_ptr<std::atomic<bool>> eventWaiterRunning_;
    void startEventWaiter();
    void stopEventWaiter();
    // When the delegation renewal was last considered (never = 0).
    qint64 lastTransientCheckMs_ = 0;
    qint64 lastApprovalCheckMs_ = 0;
    // Outgoing messages accepted by our server but not yet confirmed delivered:
    // local message id -> server attempt id, reconciled on each sync.
    std::map<qint64, std::string> pendingSends_;
    // Set true to abort in-flight downloads (teardown / session switch); the fetch
    // polls it to close a parked read and stop retrying, and queued tasks skip
    // emitting onto a tearing-down session.
    std::atomic<bool> downloadsCancelled_{false};
    // Attachment downloads run here, off the worker thread, so a long or stalled
    // fetch never blocks sends, uploads or sync. Declared last so it is drained
    // before session_ is destroyed; its tasks capture session_ and the cancel flag.
    QThreadPool downloadPool_;
};

// QML-facing facade: owns the worker thread, the models and the transcript
// store; exposes invokable commands and observable properties.
class SessionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY identityChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY identityChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionNote READ connectionNote NOTIFY connectedChanged)
    // online: this account is syncing in the background (receiving). reachable:
    // the last sync actually reached the facade. Together they give the live
    // connection status shown in the account list.
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool reachable READ reachable NOTIFY reachableChanged)
    // Why the last sync failed, empty while it is succeeding.
    Q_PROPERTY(QString syncError READ syncError NOTIFY reachableChanged)
    // The server has this account but does not serve it yet: an operator has to
    // let it in. Connected and reachable are both true meanwhile, so without this
    // the app looks healthy while nothing it sends can leave.
    // A destination action (publish, take offline) is under way. The worker may
    // be minutes deep in a sync before it gets to it, so the press has to show
    // somewhere or it reads as a button that does nothing.
    Q_PROPERTY(bool i2pBusy READ i2pBusy NOTIFY i2pStatusChanged)
    // This account's devices, newest answer first asked for.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // Recording a voice message, and how long it has been running.
    Q_PROPERTY(bool voiceRecording READ voiceRecording NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voiceElapsedMs READ voiceElapsedMs NOTIFY voiceChanged)
    // What the microphone is picking up right now, 0..1. A microphone that is
    // not working holds it at zero, which draws as a flat line.
    Q_PROPERTY(qreal voiceLevel READ voiceLevel NOTIFY voiceChanged)
    // The recording that has been stopped and is waiting to be sent or dropped:
    // whether there is one, how long it runs, what it weighs and what it looks
    // like. Nothing is sent until the user says so.
    Q_PROPERTY(bool voiceTakeReady READ voiceTakeReady NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voiceTakeMs READ voiceTakeMs NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voiceTakeBytes READ voiceTakeBytes NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceTakeWave READ voiceTakeWave NOTIFY voiceChanged)
    Q_PROPERTY(bool voiceTakePlaying READ voiceTakePlaying NOTIFY voiceChanged)
    // Why recording could not start (no microphone, usually); empty when fine.
    Q_PROPERTY(QString voiceError READ voiceError NOTIFY voiceChanged)
    // Playback speed for voice messages, cycled by the bubble's own control.
    Q_PROPERTY(qreal voiceSpeed READ voiceSpeed NOTIFY voiceChanged)
    // The message whose voice note is playing, empty when none is.
    Q_PROPERTY(QString voicePlaying READ voicePlaying NOTIFY voiceChanged)
    Q_PROPERTY(bool awaitingApproval READ awaitingApproval NOTIFY approvalChanged)
    // What the operator tells a user who is waiting (empty if they wrote none).
    Q_PROPERTY(QString approvalNote READ approvalNote NOTIFY approvalChanged)
    // The facade the transport is connected/connecting through, and the full
    // configured facade list (for the connection editor and status display).
    Q_PROPERTY(QString activeFacade READ activeFacade NOTIFY facadeInfoChanged)
    // Just the host of the active facade: what the status line shows, where the
    // scheme and base path only cost characters of an already long b32 name.
    Q_PROPERTY(QString activeFacadeHost READ activeFacadeHost NOTIFY facadeInfoChanged)
    Q_PROPERTY(QStringList configuredFacades READ configuredFacades NOTIFY facadeInfoChanged)
    // Whether any configured facade is an I2P facade (host ends in ".b32.i2p"). When
    // full privacy mode is on and this is false, the profile cannot reach its server
    // (clearnet is refused), so its status reads as an explicit I2P-only offline error.
    Q_PROPERTY(bool hasI2pFacade READ hasI2pFacade NOTIFY facadeInfoChanged)
    // Whether traffic is currently leaving over I2P. False while connected on a
    // clearnet facade - which the chat view says out loud, because it is a
    // downgrade the user did not ask for.
    Q_PROPERTY(bool onI2p READ onI2p NOTIFY facadeInfoChanged)
    Q_PROPERTY(bool clearnetAllowed READ clearnetAllowed NOTIFY facadeInfoChanged)
    // Connect-in-flight state for the connect screen: whether a connect is
    // running, what it is doing, and why the last one failed.
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectStateChanged)
    Q_PROPERTY(QString connectPhase READ connectPhase NOTIFY connectStateChanged)
    Q_PROPERTY(int connectPercent READ connectPercent NOTIFY connectStateChanged)
    Q_PROPERTY(QString connectError READ connectError NOTIFY connectStateChanged)
    // The configured server's fingerprint, so the connection editor can prefill it.
    Q_PROPERTY(QString serverFingerprint READ serverFingerprint NOTIFY facadeInfoChanged)
    Q_PROPERTY(QString activePeer READ activePeer NOTIFY activePeerChanged)
    // The active peer's display name (the local label, or a short fingerprint when
    // unnamed). Notified on both opening a conversation and a rename, so the chat
    // header stays current.
    Q_PROPERTY(QString activePeerName READ activePeerName NOTIFY activePeerNameChanged)
    // Paging state of the open conversation: whether the newest page is loaded
    // (so stick-to-bottom applies) and whether older history remains above.
    Q_PROPERTY(bool atNewest READ atNewest NOTIFY pagingChanged)
    Q_PROPERTY(bool hasMoreOlder READ hasMoreOlder NOTIFY pagingChanged)
    // The on-disk profile id this session was opened from (stable per account).
    Q_PROPERTY(QString accountId READ accountId CONSTANT)
    // Total unread across this account's conversations (for the switcher badge).
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadTotalChanged)
    Q_PROPERTY(QObject* contacts READ contacts CONSTANT)
    // The chat list as a name-filtered view of `contacts` (drives the search box);
    // `contacts` itself stays unfiltered for pickers (new chat).
    Q_PROPERTY(QObject* chatList READ chatList CONSTANT)
    Q_PROPERTY(QObject* conversation READ conversation CONSTANT)
    // The background-activity model + the count of still-running operations (drives
    // the floating activity button's visibility).
    Q_PROPERTY(QObject* operations READ operations CONSTANT)
    Q_PROPERTY(int activeOperations READ activeOperations NOTIFY operationsChanged)
    Q_PROPERTY(bool sendReceipts READ sendReceipts WRITE setSendReceipts NOTIFY sendReceiptsChanged)
    // Whether this profile takes incoming calls. Off, a caller is refused at once
    // instead of ringing; their call button stays, because this can be turned back
    // on at any moment. Kept with the profile, not with the window.
    Q_PROPERTY(bool acceptCalls READ acceptCalls WRITE setAcceptCalls NOTIFY acceptCallsChanged)
    // True while the composer is editing a previously sent message; editingText
    // is its current text, so the composer can prefill the field.
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString editingText READ editingText NOTIFY editingChanged)
    // True while composing a reply; replyingText/replyingSender describe the quoted
    // original so the composer can show a reply banner.
    Q_PROPERTY(bool replying READ replying NOTIFY replyingChanged)
    Q_PROPERTY(QString replyingText READ replyingText NOTIFY replyingChanged)
    Q_PROPERTY(QString replyingSender READ replyingSender NOTIFY replyingChanged)
    // Bumped whenever the contact set/state changes, so a contact-request bubble's
    // "Agree" button re-evaluates contactCanAccept() after an accept.
    Q_PROPERTY(int contactsRevision READ contactsRevision NOTIFY contactsRevisionChanged)
    // Bumped whenever any reaction changes, so a message bubble's reaction chips
    // and the who-reacted modal re-query the store.
    Q_PROPERTY(int reactionsRevision READ reactionsRevision NOTIFY reactionsRevisionChanged)
    // Per-user I2P destination status for the settings page.
    Q_PROPERTY(bool i2pHasKey READ i2pHasKey NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pEnabled READ i2pEnabled NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pActive READ i2pActive NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString i2pAddress READ i2pAddress NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString i2pStatusText READ i2pStatusText NOTIFY i2pStatusChanged)
    // What the messaging server says about the destination itself: "active",
    // "building", "none", or empty when it has not been asked yet.
    Q_PROPERTY(QString i2pServerState READ i2pServerState NOTIFY i2pStatusChanged)
    // The contact request currently being agreed to, empty when none is in
    // flight. Accepting is a server round trip, so the button says so instead of
    // vanishing and coming back.
    Q_PROPERTY(QString acceptingContact READ acceptingContact NOTIFY acceptingContactChanged)
    // Unix second the personal destination is paid through (0 when inactive), so
    // the settings page can show an expiry date or the phrase "Inactive".
    Q_PROPERTY(qint64 i2pTransientExpires READ i2pTransientExpires NOTIFY i2pStatusChanged)
    // This profile's storage usage for the settings view: a map with mailboxOk,
    // mailboxUsed, mailboxQuota (bytes), updatedAt (the
    // unix-ms time it was last fetched, 0 if never) and everFetched. The figures
    // persist for the session, so an offline profile still shows its last-known
    // usage with a "updated N ago" age.
    Q_PROPERTY(QVariantMap storageInfo READ storageInfo NOTIFY storageChanged)
    // Audio call state for the call screen: "idle"/"outgoing"/"incoming"/"active",
    // the peer fingerprint, a display name, and the local mute flag.
    Q_PROPERTY(QString callState READ callState NOTIFY callChanged)
    // What the caller is waiting on (delivering the invitation, ringing, opening
    // the audio path); empty once the call is running. And when it started.
    Q_PROPERTY(QString callStage READ callStage NOTIFY callChanged)
    Q_PROPERTY(qint64 callConnectedAtMs READ callConnectedAtMs NOTIFY callChanged)
    Q_PROPERTY(QString callPeer READ callPeer NOTIFY callChanged)
    Q_PROPERTY(QString callPeerName READ callPeerName NOTIFY callChanged)
    Q_PROPERTY(bool callMuted READ callMuted NOTIFY callChanged)
    // and the remote peer). Stable for the controller's lifetime.
public:
    explicit SessionController(QObject* parent = nullptr);
    ~SessionController() override;

    QString fingerprint() const { return fingerprint_; }
    QString displayName() const { return displayName_; }
    bool connected() const { return connected_; }
    QString connectionNote() const { return connectionNote_; }
    bool online() const { return online_; }
    bool reachable() const { return reachable_; }
    QString syncError() const { return syncError_; }
    bool i2pBusy() const { return i2pBusy_; }
    QVariantList devices() const { return devices_; }
    bool voiceRecording() const { return voiceRecording_; }
    qint64 voiceElapsedMs() const { return voiceElapsedMs_; }
    qreal voiceLevel() const { return voiceLevel_; }
    bool voiceTakeReady() const { return !voiceTake_.isEmpty(); }
    qint64 voiceTakeMs() const { return voiceTakeMs_; }
    qint64 voiceTakeBytes() const { return voiceTake_.size(); }
    QString voiceTakeWave() const { return voiceTakeWave_; }
    bool voiceTakePlaying() const { return voiceTakePlaying_; }
    QString voiceError() const { return voiceError_; }
    qreal voiceSpeed() const;
    QString voicePlaying() const { return voicePlaying_; }
    bool awaitingApproval() const { return awaitingApproval_; }
    QString approvalNote() const { return approvalNote_; }
    QString activeFacade() const { return activeFacade_; }
    QString activeFacadeHost() const;
    QStringList configuredFacades() const { return configuredFacades_; }
    bool hasI2pFacade() const;
    bool onI2p() const { return activeFacade_.contains(QStringLiteral(".b32.i2p")); }
    bool clearnetAllowed() const { return clearnetAllowed_; }
    bool connecting() const { return connecting_; }
    QString connectPhase() const { return connectPhase_; }
    int connectPercent() const { return connectPercent_; }
    QString connectError() const { return connectError_; }
    QString serverFingerprint() const { return serverFp_; }
    QString activePeer() const { return activePeer_; }
    QString activePeerName() const { return peerName(activePeer_); }
    bool atNewest() const;
    bool hasMoreOlder() const;
    QString accountId() const { return profileId_; }
    int unreadTotal() const { return unreadTotal_; }
    QObject* contacts() { return &contacts_; }
    QObject* chatList() { return &contactsProxy_; }
    // Filters the chat list by name (case-insensitive substring); empty shows all.
    Q_INVOKABLE void setChatFilter(const QString& text);
    // Pin/unpin a chat to the top of the list (synced to the account's other
    // devices); isChatPinned drives the menu label + the row's pin marker.
    Q_INVOKABLE void pinChat(const QString& peer, bool pinned);
    Q_INVOKABLE bool isChatPinned(const QString& peer) const;
    QObject* conversation() { return &conversation_; }
    QObject* operations() { return &operations_; }
    int activeOperations() const { return operations_.runningCount(); }
    bool acceptCalls() const { return acceptCalls_; }
    void setAcceptCalls(bool on);
    bool sendReceipts() const { return sendReceipts_; }
    void setSendReceipts(bool on) { if (sendReceipts_ != on) { sendReceipts_ = on; emit sendReceiptsChanged(); } }
    bool editing() const { return editing_; }
    QString editingText() const { return editingText_; }
    bool replying() const { return replying_; }
    QString replyingText() const { return replyingText_; }
    QString replyingSender() const { return replyingSender_; }
    int contactsRevision() const { return contactsRevision_; }
    int reactionsRevision() const { return reactionsRevision_; }
    bool i2pHasKey() const { return i2pHasKey_; }
    bool i2pEnabled() const { return i2pEnabled_; }
    bool i2pActive() const { return i2pActive_; }
    QString i2pAddress() const { return i2pAddress_; }
    QString i2pStatusText() const { return i2pStatusText_; }
    QString i2pServerState() const { return i2pServerState_; }
    QString acceptingContact() const { return acceptingContact_; }
    QVariantMap storageInfo() const;
    qint64 i2pTransientExpires() const { return i2pTransientExpires_; }
    QString callState() const { return callState_; }
    QString callStage() const { return callStage_; }
    qint64 callConnectedAtMs() const { return callConnectedAtMs_; }
    QString callPeer() const { return callPeer_; }
    QString callPeerName() const { return peerName(callPeer_); }
    bool callMuted() const { return callMuted_; }

    // Opens a profile on the worker thread (dir + id + passphrase).
    void open(const QString& file, const QString& profileId, const QString& passphrase);

    // Connects (and subscribes) through an ordered list of facade URLs
    // (http[s]://host[:port][/secret]). The client fails over across them.
    Q_INVOKABLE void connectServer(const QStringList& facadeUrls, const QString& serverFp);
    // Decodes a bazarish://server/... link into { serverFp, facades } for the
    // connect form to prefill; returns an empty map on a malformed link.
    Q_INVOKABLE QVariantMap parseServerLink(const QString& uri) const;
    // Brings this account online (resume syncing) or offline (stop syncing
    // without unloading it).
    Q_INVOKABLE void goOnline();
    Q_INVOKABLE void goOffline();
    // Drops the I2P destinations this account holds so they are built again with
    // the tunnel profile now in force.
    void rebuildI2pLinks();
    Q_INVOKABLE void openConversation(const QString& peer);
    // Leaves the open conversation without opening another: on a narrow window
    // the chat is the whole window, so there has to be a way back to the list.
    Q_INVOKABLE void closeConversation();
    // Opens a conversation positioned at a specific message (a search hit): loads
    // a window ending at it and asks the view to scroll there.
    Q_INVOKABLE void openConversationAtMessage(const QString& peer, qint64 messageId);
    // Pages the open conversation: loads the next batch of older (top) / newer
    // (bottom) messages into the model and returns how many were added.
    Q_INVOKABLE int loadOlderMessages();
    Q_INVOKABLE int loadNewerMessages();
    // Returns to the newest page (reloading it if the window was scrolled back).
    Q_INVOKABLE void jumpToLatest();
    // Remembers / restores the open conversation's scroll position, so switching
    // this account out and back brings the dialog back to where it was left (or
    // keeps it pinned to the bottom if it was). The view saves the top-visible row
    // index as the user scrolls (-1 when pinned to the bottom), keyed by peer, and
    // restores it when this account becomes active again.
    Q_INVOKABLE void saveScroll(const QString& peer, int anchorRow, bool stick);
    Q_INVOKABLE QVariantMap scrollFor(const QString& peer) const;
    // The user is reading: the view is open, the window is focused and scrolled so
    // that `row` is the bottom-most visible row. Sends a read receipt (green) for
    // the newest incoming message at or before it (high-water, deduped per peer).
    Q_INVOKABLE void markReadThroughRow(int row);
    // Case-insensitive full-text search of the open conversation; returns a list
    // of {id, text, time, outgoing, author} maps for the search popup.
    Q_INVOKABLE QVariantList searchMessages(const QString& query);
    Q_INVOKABLE void sendText(const QString& text);
    // Re-dispatches a failed outgoing text message (same protocol id) after the
    // user taps "Resend" on its bubble.
    Q_INVOKABLE void resendText(qint64 localId, const QString& text, const QString& protocolId);
    // Sends a picked file. Nothing to choose: the bytes go device to device over a
    // one-time destination, so there is no store to keep them in and no retention
    // to set; only the offer travels through the servers.
    Q_INVOKABLE void sendFile(const QString& fileUrl);
    // The same transfer announced as a picture: the recipient fetches and shows
    // it instead of being offered a Save button.
    Q_INVOKABLE void sendPicture(const QString& fileUrl);
    // Writes a picture this profile holds out to a file the user chose.
    Q_INVOKABLE void savePictureAs(const QString& messageId, const QString& fileUrl);
    // Puts it on the clipboard as an image: it goes from memory to memory, and
    // never becomes a plaintext file on the way.
    Q_INVOKABLE void copyPicture(const QString& messageId);

    // --- Voice messages ---
    //
    // Recorded from the microphone, encoded with the same codec a call uses, and
    // sent inside the message: small enough to ride there, so nothing is
    // announced and nothing is fetched.
    // Recording runs in three steps, because a voice message is confirmed before
    // it goes: record, stop (which holds the take), then send or drop it.
    Q_INVOKABLE void startVoiceRecording();
    Q_INVOKABLE void stopVoiceRecording();
    Q_INVOKABLE void cancelVoiceRecording();
    // Listening to the held take before sending it, and sending or dropping it.
    Q_INVOKABLE void playVoiceTake();
    Q_INVOKABLE void stopVoiceTake();
    Q_INVOKABLE void sendVoiceTake();
    Q_INVOKABLE void discardVoiceTake();
    Q_INVOKABLE void playVoice(const QString& messageId);
    Q_INVOKABLE void stopVoice();
    // Steps the playback speed through the offered rates and back to normal.
    Q_INVOKABLE void cycleVoiceSpeed();
    // A name to suggest for that file.
    Q_INVOKABLE QUrl defaultPictureSaveUrl(const QString& messageId, const QString& name) const;
    // Re-dispatches a failed outgoing file from the saved source path (reusing the
    // bubble); if that file is gone, emits resendFilePickRequested so the UI can
    // offer to pick a file to send instead.
    Q_INVOKABLE void resendFile(qint64 localId, const QString& protocolId);
    // A display name for any peer.
    Q_INVOKABLE QString peerName(const QString& id) const;
    // The contact's stored local display name, empty when unnamed (so a rename
    // field can prefill it and show a fingerprint placeholder otherwise).
    Q_INVOKABLE QString contactName(const QString& fp) const;
    // Sets the account's own avatar from a picked image file (file:// URL).
    Q_INVOKABLE void setAvatar(const QString& fileOrUrl);
    // Drops the account's avatar. Contacts keep the copy they hold until a new one
    // is pushed; this device stops showing one.
    Q_PROPERTY(bool hasAvatar READ hasAvatar NOTIFY avatarChanged)
    bool hasAvatar() const;
    // Whether an avatar change is in flight: compressing and handing the picture
    // to every contact takes seconds over I2P, and the settings page says so
    // where the picture is rather than leaving the click without an answer.
    Q_PROPERTY(bool avatarBusy READ avatarBusy NOTIFY avatarChanged)
    bool avatarBusy() const { return avatarBusy_; }
    // Emitted when this account's own avatar is set, dropped, or starts changing.
    Q_INVOKABLE void clearAvatar();
    // Changes the account's own display name (trimmed). Local only: updates this
    // device and the name carried in future invite descriptors; contacts are not
    // told (each keeps their own local name for us).
    Q_INVOKABLE void setDisplayName(const QString& name);
    // --- Reactions + read receipts ---
    // Sets our reaction emoji on a message (by its protocol id) in the active chat:
    // optimistic local store + send. Tapping the emoji we already set removes it.
    Q_INVOKABLE void react(const QString& protocolId, const QString& emoji);
    // Reactions this user reached for that are not in the standard set, newest
    // first. Kept per profile so the picker offers what this person actually uses.
    // The set the picker offers by default. Held here because it also decides
    // what counts as "one of this user's own" for the recents below, and that
    // decision has to be the same wherever a reaction is set from.
    Q_PROPERTY(QStringList standardReactions READ standardReactions CONSTANT)
    QStringList standardReactions() const;
    Q_PROPERTY(QStringList recentReactions READ recentReactions NOTIFY recentReactionsChanged)
    QStringList recentReactions() const { return recentReactions_; }
    Q_INVOKABLE void rememberReaction(const QString& emoji);
    // Our current reaction emoji on a message (empty when none) - for the toggle.
    Q_INVOKABLE QString myReaction(const QString& protocolId) const;
    // The reaction chips for a message: a list of { emoji, count, mine } aggregated
    // across reactors, in first-seen order.
    Q_INVOKABLE QVariantList reactionSummary(const QString& protocolId) const;
    // Renames a contact locally (mirrored to the account's own other devices).
    Q_INVOKABLE void renameContact(const QString& fp, const QString& name);
    // A shareable link for a contact we hold: the same artifact as our own
    // invite, built from what they already gave us. Empty while we hold no
    // routing for them.
    Q_INVOKABLE QString contactInvite(const QString& fp) const;
    // Messages this device can still send that contact before it asks them for
    // more capacity (their one-time delivery tokens we hold).
    Q_INVOKABLE int sendCapacity(const QString& fp) const;
    // Our own invite, from what this profile already holds: the fingerprint and
    // the address are ours, and the server's serving key has been in the stored
    // certificate since the destination was first published. Empty only while
    // that has never happened.
    Q_PROPERTY(QString ownInvite READ ownInvite NOTIFY ownInviteChanged)
    QString ownInvite() const { return ownInvite_; }
    // Clears the active 1:1 conversation. forEveryone also asks the peer to clear
    // their copy (chat.clear); the chat row itself remains.
    Q_INVOKABLE void clearChat(bool forEveryone);
    // Permanently deletes the active contact and its whole chat (irreversible).
    Q_INVOKABLE void deleteContact();
    // Inline-keyboard button presses in the active conversation: a callback
    // (button data + the keyboard message's protocol id) or a command button.
    Q_INVOKABLE void sendCallback(const QString& data, const QString& refMsgId);
    Q_INVOKABLE void sendCommand(const QString& command, const QString& args);
    // Editing one's own message: start (prefilling the composer), commit the new
    // text (updates our copy and sends an edit to the peer), or cancel.
    Q_INVOKABLE void beginEdit(qint64 localId, const QString& protocolId, const QString& text);
    Q_INVOKABLE void commitEdit(const QString& newText);
    Q_INVOKABLE void cancelEdit();
    // Reply: start replying to a message (the composer shows a quote banner), or
    // cancel. The next sent message carries the referenced protocol id.
    Q_INVOKABLE void beginReply(
        const QString& protocolId, const QString& previewText, const QString& sender);
    Q_INVOKABLE void cancelReply();
    // Resolves a reply reference to the original message in the open conversation:
    // { found, localId, text, sender } so a bubble can render a clickable quote.
    Q_INVOKABLE QVariantMap replyPreview(const QString& protocolId) const;
    // Deletes a message with no trace. The local copy is always removed; for one's
    // own one-to-one message (outgoing, protocolId set) the recipient is asked to
    // remove its copy too. A received message is removed locally only.
    Q_INVOKABLE void deleteMessage(qint64 localId, const QString& protocolId, bool outgoing);
    // Copies arbitrary text (a whole message) to the system clipboard.
    Q_INVOKABLE void copyText(const QString& text) const;
    // Why this text is not a usable invite, or empty when it parses. Local and
    // instant: a paste that cannot work must be refused at the field, not by a
    // background operation that dials I2P first.
    Q_INVOKABLE QString inviteProblem(const QString& uri) const;
    Q_INVOKABLE void addByInvite(const QString& uri, const QString& intro);
    Q_INVOKABLE void addByUsername(const QString& alias, const QString& intro);
    // Agrees to the active chat's received contact request (the green "Agree").
    Q_INVOKABLE void acceptContact();
    // Whether `fp` is a contact that sent us a request we have not yet accepted
    // (drives the "Agree" button on an incoming contact-request bubble).
    Q_INVOKABLE bool contactCanAccept(const QString& fp) const;
    Q_INVOKABLE void requestInvite();
    // Signs a sign-in-with-key challenge with this profile's key (no server
    // needed); the result arrives via loginSigned(). The key never leaves the app.
    Q_INVOKABLE void signLogin(const QString& challenge);
    Q_INVOKABLE void saveAttachment(const QString& peer, const QString& messageId, const QString& fileUrl);
    // Saves a received attachment to the file the user picked in the native Save
    // dialog (which already resolved any name conflict), reporting byte progress
    // and the outcome back onto the message identified by token.
    // A file is fetched from the peer that announced it, by the announcing
    // message's protocol id - there is no store to fetch it from.
    Q_INVOKABLE void saveAttachmentToFile(const QString& peer, const QString& messageId,
        const QString& fileUrl, qint64 token);
    // A suggested save location (the Downloads folder joined with fileName) as a
    // file URL, used to pre-fill the native Save dialog's name and folder.
    Q_INVOKABLE QUrl defaultSaveUrl(const QString& fileName) const;
    // Whether a saved attachment still exists on disk (drives Save vs Open).
    Q_INVOKABLE bool fileExists(const QString& path) const;
    // Reveals a saved attachment in the system file manager with the file itself
    // selected (falling back to opening its folder).
    Q_INVOKABLE void showInFolder(const QString& path) const;
    Q_INVOKABLE void exportProfile(const QString& fileUrl, const QString& password);
    Q_INVOKABLE QString shortFingerprint(const QString& fp) const;
    // Per-user I2P destination controls (drive the worker thread).
    Q_INVOKABLE void generatePersonalKey();
    Q_INVOKABLE void loadPersonalKey(const QString& fileUrl);
    Q_INVOKABLE void deletePersonalKey();
    // Sticky I2P's escape hatch: this profile has reached its server over I2P and
    // refuses clearnet since; allowing it again is the user's call, never automatic.
    Q_INVOKABLE void allowClearnet(bool allow);
    // Stops a file transfer in either direction, by the file's protocol id (the
    // activity panel offers this on a running transfer).
    Q_INVOKABLE void cancelTransfer(const QString& protocolId);
    Q_INVOKABLE void publishPersonalDest();
    Q_INVOKABLE void disablePersonalDest();
    Q_INVOKABLE void refreshI2pStatus();
    // Triggers a fresh poll of this profile's storage usage (mailbox + blob). The
    // result lands in the storageInfo property; until it does, the last figures (if
    // any) stay, with the UI showing how long ago they were taken.
    Q_INVOKABLE void refreshStorageUsage();
    Q_INVOKABLE void refreshDevices();
    // Drops a device's registration: its unacked mail stops being held, and the
    // device registers again the next time it connects.
    Q_INVOKABLE void forgetDevice(const QString& clientId);
    // Audio calls. startCall dials the active/given peer; accept/decline act on
    // the current incoming call; end hangs up; setCallMuted toggles the mic.
    Q_INVOKABLE void startCall(const QString& peer);
    Q_INVOKABLE void acceptCall();
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void endCall();
    Q_INVOKABLE void setCallMuted(bool muted);

signals:
    void recentReactionsChanged();
    void avatarChanged();
    void identityChanged();
    void connectedChanged();
    void activePeerChanged();
    void activePeerNameChanged();
    // The open conversation's paging state changed (atNewest / hasMoreOlder).
    void pagingChanged();
    // Asks the view to scroll the given message into view (a search jump).
    void scrollToMessage(qint64 messageId);
    // Asks the view to position the first unread message near the top and briefly
    // highlight the unread tail (a conversation opened with unread messages).
    void scrollToUnread(qint64 firstUnreadId);
    // Asks the view to scroll to the bottom (jump-to-latest).
    void scrollToBottom();
    void facadeInfoChanged();
    void connectStateChanged();
    void sendReceiptsChanged();
    void acceptCallsChanged();
    void editingChanged();
    void replyingChanged();
    void contactsRevisionChanged();
    void reactionsRevisionChanged();
    void unreadTotalChanged();
    void operationsChanged();
    void onlineChanged();
    void reachableChanged();
    void approvalChanged();
    void acceptingContactChanged();
    void ownInviteChanged();
    void i2pStatusChanged();
    void devicesChanged();
    void voiceChanged();
    void storageChanged();
    void callChanged();
    void openFailed(const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);
    void inviteUnavailable(const QString& reason);
    void loginSigned(const QString& blob);
    // A failed file's saved source is gone: the UI should offer to pick a file.
    void resendFilePickRequested();
    // Forwarded onboarding info for the hello dialog (unregistered-key connect).
    void serverHello(const QString& reason, const QString& message, const QStringList& links);

signals:  // to worker
    void requestConnect(const QStringList& facadeUrls, const QString& serverFp);
    void requestSendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void requestSendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void requestSendPicture(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void requestSendVoice(const QString& peer, const QByteArray& opus, qint64 durationMs,
        qint64 localId, const QString& protocolId, const QString& replyTo);
    void requestSendReceipt(const QString& peer, const QString& refId);
    void requestAckPending(const QString& pendingId);
    void requestSendReaction(const QString& peer, const QString& refId, const QString& emoji);
    void requestSendCallback(const QString& peer, const QString& data, const QString& ref);
    void requestSendCommand(const QString& peer, const QString& command, const QString& args);
    void requestSendEdit(const QString& peer, const QString& refId, qint64 localId,
        const QString& text);
    void requestSendDelete(const QString& peer, const QString& refId);
    void requestSetAvatar(const QString& localPath);
    void requestClearAvatar();
    void requestSetDisplayName(const QString& name);
    void requestRenameContact(const QString& peer, const QString& name);
    void requestRemoveContact(const QString& peer);
    void requestSyncChatPin(const QString& peer, bool pinned);
    void requestClearChatForEveryone(const QString& peer);
    void requestAddByInvite(const QString& uri, const QString& intro, const QString& opId);
    void requestAddByUsername(const QString& alias, const QString& intro, const QString& opId);
    void requestAcceptContact(const QString& peer);
    void requestInviteSig();
    void requestSignLoginSig(const QString& challenge);
    void requestSaveAttachment(const QString& ref, const QString& key, const QString& destPath,
        qint64 token);
    void requestExport(const QString& path, const QString& password);
    void requestOpen(const QString& dir, const QString& passphrase);
    void requestSetSync(bool on);
    void requestRebuildI2p();
    void requestCancelTransfer(const QString& protocolId);
    void requestGeneratePersonalKey();
    void requestLoadPersonalKey(const QString& path);
    void requestDeletePersonalKey();
    void requestAllowClearnet(bool allow);
    void requestSetAcceptCalls(bool accept);
    void requestPublishPersonalDest();
    void requestDisablePersonalDest();
    void requestRefreshI2pStatus();
    void requestRefreshStorageUsage();
    void requestRefreshDevices();
    void requestForgetDevice(const QString& clientId);
    void requestStartCall(const QString& peer);
    void requestAcceptCall(const QString& callId);
    void requestDeclineCall(const QString& callId);
    void requestEndCall();
    void requestSetCallMuted(bool muted);

private slots:
    void onOpened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& connectionNote);
    void onConnectionChanged(bool connected, const QString& connectionNote);
    void onMessageReceived(const QVariantMap& message);
    // Connected to messageReceived AFTER onMessageReceived, so it runs once that has
    // durably stored/handled the item: acks the pending mailbox item (deferred ack),
    // so a crash between fetch and store never loses a message.
    void ackAfterReceive(const QVariantMap& message);
    void onAvatarReady(const QString& fingerprint, const QByteArray& data);
    void onContactAddStage(const QString& opId, const QString& status);
    // Opens the conversation for a contact being added and starts a note in it.
    void openContactProgress(const QString& peer, const QString& opId, const QString& name);
    void writeContactProgress(const QString& opId, const QString& text);
    void onContactAddDone(const QString& opId, bool ok, const QString& status);
    void onContactAccepted(const QString& peer, bool ok, const QString& reason);
    void onOpBegin(const QString& opId, const QString& kind, const QString& title,
        const QString& status);
    void onOpDone(const QString& opId, bool ok, const QString& status);
    void onSendProgress(qint64 localId, int state);
    void onUploadProgress(qint64 localId, qint64 sent, qint64 total);
    void onDownloadProgress(qint64 token, qint64 received, qint64 total);
    void onServedProgress(const QString& peer, const QString& protocolId, qint64 sent,
        qint64 total);
    void onTransferStage(const QString& peer, const QString& protocolId, const QString& stage);
    void onServedFinished(const QString& peer, const QString& protocolId, bool ok,
        const QString& error);
    void onDownloadStage(qint64 token, int stage);
    void onDownloadFinished(qint64 token, bool ok, const QString& error);
    void onSendResult(qint64 localId, bool ok, const QString& error);
    void onSendSettled(qint64 localId, const QString& note);
    void onSendPhase(qint64 localId, const QString& phase);
    void onContactRequestSent(const QString& fingerprint, const QString& intro);
    void onSyncReachable(bool ok, const QString& reason);
    void onApprovalState(bool pending, const QString& note);
    void onConnectProgress(int percent, const QString& phase);
    void onFacadeInfo(
        const QString& activeUrl, const QStringList& configured, const QString& serverFp);
    void onI2pStatus(bool hasKey, bool enabled, bool active, const QString& address,
        const QString& summary, qint64 transientExpires, const QString& serverState);
    void onI2pKeyState(bool hasKey, const QString& address);
    void onDevicesReady(const QVariantList& devices);
    void onVoiceLoaded(const QString& messageId, const QByteArray& bytes);
    // Pulls a small incoming picture into the media cache without being asked.
    void requestPicturesFor(const QList<StoredMessage>& messages);
    void onStorageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota);
    void onCallStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, qint64 connectedAtMs);
    // Appends a finished call to the peer's transcript as a clear system line.
    void onCallLogged(const QString& peer, bool incoming, int outcome, qint64 durationSec);

private:
    QThread thread_;
    SessionWorker* worker_ = nullptr;
    TranscriptStore store_;
    ContactListModel contacts_;
    // Name-filtered view over contacts_ for the chat-list search (source order, so
    // the pinned-first/recent sort from the source model is preserved).
    QSortFilterProxyModel contactsProxy_;
    ConversationModel conversation_;
    // Live background operations shown in the activity panel. A finished row
    // lingers briefly (so the result is visible) and is then auto-removed.
    OperationListModel operations_;
    // Begin/update/finish a background operation by a stable id; finishOperation
    // marks it done/failed and schedules its removal. operationsChanged fires
    // whenever the running count may have changed (button visibility).
    void beginOperation(const QString& id, const QString& kind, const QString& title,
        const QString& status, const QString& peer = {},
        const QString& cancelId = {});
    void updateOperation(const QString& id, const QString& status, const QString& detail = {},
        double progress = -1.0);
    void finishOperation(const QString& id, bool ok, const QString& finalStatus);

    QString profileId_;
    QString fingerprint_;
    QString displayName_;
    bool connected_ = false;
    bool online_ = false;
    bool reachable_ = false;
    QString syncError_;
    bool i2pBusy_ = false;
    bool awaitingApproval_ = false;
    QString approvalNote_;
    QString connectionNote_;
    QString activePeer_;
    QString activeFacade_;
    bool avatarBusy_ = false;
    QStringList configuredFacades_;
    QString serverFp_;
    bool sendReceipts_ = true;
    bool acceptCalls_ = true;
    // Edit-in-progress state for the composer (0 / empty when not editing).
    bool editing_ = false;
    qint64 editingLocalId_ = 0;
    QString editingProtocolId_;
    QString editingText_;
    // Reply-in-progress state for the composer (empty when not replying).
    bool replying_ = false;
    QString replyingProtocolId_;
    QString replyingText_;
    QString replyingSender_;
    // Contacts we received a request from but have not accepted yet (their
    // fingerprints), so a request bubble can offer "Agree". Refreshed from the
    // worker; contactsRevision_ bumps on every refresh to re-drive the binding.
    QSet<QString> pendingContacts_;
    int contactsRevision_ = 0;
    // Bumped on any reaction change so QML re-queries the store.
    int reactionsRevision_ = 0;
    // Current delivery status per outgoing local id, so a later/lower signal
    // (e.g. "yellow" arriving after "green") never downgrades the tick.
    QHash<qint64, int> statusById_;
    void bumpStatus(qint64 localId, int status);
    void setAvatarBusy(bool busy);
    // Conversation paging window. The model holds only [oldestLoadedId_ ..
    // newestLoadedId_]; the has-more flags say whether the store has rows beyond
    // either edge (drives load-more and the jump-to-latest control).
    qint64 oldestLoadedId_ = 0;
    qint64 newestLoadedId_ = 0;
    bool hasMoreOlder_ = false;
    bool hasMoreNewer_ = false;
    // Saved scroll position of the open conversation so switching this account out
    // and back restores the view instead of jumping to the top. Peer-keyed; stick
    // means it rested at the bottom (restore by pinning, not by the saved row).
    QString scrollPeer_;
    int scrollAnchorRow_ = -1;
    bool scrollStick_ = true;
    // Sets the active peer + clears unread, without touching the message window
    // (the caller chooses which window to load).
    void activateConversation(const QString& peer);
    // Loads the newest page into the model and resets the paging window.
    void loadLatestWindow();
    // Loads a page anchored at the first unread message (it sits at the window's
    // oldest edge, so the unread block flows down from the top) and asks the view to
    // scroll there and highlight the unread tail.
    void openWindowAtUnread(const QString& peer, qint64 firstUnread);
    // Adds a just-stored message to the open conversation's window when the window
    // is at the newest edge; isOwn jumps to the newest page if it was scrolled back.
    void showInActiveView(const StoredMessage& m, bool isOwn);
    // Marks our outgoing messages to peer with id <= uptoId as read (green), in
    // the store and the open window, on receiving a read receipt.
    void markOutgoingRead(const QString& peer, qint64 uptoId);
    // Per-peer high-water of the newest incoming message we have already sent a
    // read receipt for, so reading does not re-send receipts on every scroll tick.
    QHash<QString, qint64> lastReadAckedId_;
    // This account's contact fingerprints, kept in sync from the worker.
    QStringList contactFps_;
    // Per-contact local display names (fingerprint -> name), kept in sync from the
    // worker. Drives peerName() and the chat-list labels.
    QHash<QString, QString> contactNames_;
    QHash<QString, QString> contactLinks_;
    // Sending capacity per contact: their tokens this device still holds.
    QHash<QString, int> sendCapacities_;
    QStringList recentReactions_;
    // Where this profile lives and what unlocks it, for the store below.
    QString profilePath_;
    // The system note tracking a contact add, per operation id: the progress of a
    // request is written into the conversation it will belong to.
    QHash<QString, qint64> contactProgressRows_;
    QString profilePassphrase_;
    std::unique_ptr<client::ProfileDb> profileDb_;
    client::ProfileDb& profileDb();
    QString ownInvite_;
    // Destination chosen for an in-flight attachment save (message id -> path),
    // recorded as the saved location once the download succeeds.
    QHash<qint64, QString> pendingSavePath_;
    // Which message a picture belongs to, so one that will not decode can be
    // marked broken where it stands.
    QHash<QString, qint64> pictureOwners_;
    // Recording and playing voice messages; the audio never leaves memory. Built
    // on first use, and wired to the clock and the level the recorder shows.
    VoiceNote* voiceNote();
    std::unique_ptr<VoiceNote> voice_;
    QTimer voiceTimer_;
    bool voiceRecording_ = false;
    qint64 voiceElapsedMs_ = 0;
    qreal voiceLevel_ = 0.0;
    // The stopped recording waiting for the user to send it, with what the modal
    // shows about it. Empty when there is none.
    QByteArray voiceTake_;
    qint64 voiceTakeMs_ = 0;
    QString voiceTakeWave_;
    bool voiceTakePlaying_ = false;
    QString voiceError_;
    int voiceSpeedStep_ = 0;
    QString voicePlaying_;
    // The blob-retention chosen for each outgoing file (by local id), so a resend
    // reuses the same TTL / download cap. Session-only; a resend after a restart
    // falls back to the store default.
    bool clearnetAllowed_ = false;
    bool connecting_ = false;
    QString connectPhase_;
    int connectPercent_ = 0;
    QString connectError_;
    bool i2pHasKey_ = false;
    bool i2pEnabled_ = false;
    bool i2pActive_ = false;
    QString i2pAddress_;
    QString i2pStatusText_;
    QVariantList devices_;
    QString i2pServerState_;
    QString acceptingContact_;
    // Transfers in flight, by the file's protocol id. Kept here rather than only
    // in the conversation model, which is rebuilt whenever the user opens another
    // chat: a transfer must not lose its state because nobody was looking.
    struct TransferProgress {
        QString peer;
        QString stage;
        qint64 sent = 0;
        qint64 total = 0;
        // A finished transfer is kept until the conversation it belongs to is
        // opened: a failure that lands while the user is in another chat has to
        // reach the bubble eventually, not vanish with the live state.
        bool finished = false;
        bool ok = false;
        QString error;
    };
    QHash<QString, TransferProgress> transfers_;
    // Puts the transfers of the open conversation back on their bubbles.
    void replayTransfersForActivePeer();
    qint64 i2pTransientExpires_ = 0;
    // Last-fetched storage usage (session-scoped), with the wall-clock ms it was
    // taken so the settings view can show "updated N ago" even while offline.
    bool storageMailboxOk_ = false;
    quint64 storageMailboxUsed_ = 0;
    quint64 storageMailboxQuota_ = 0;
    qint64 storageUpdatedAtMs_ = 0;
    QString callState_ = QStringLiteral("idle");
    QString callStage_;
    qint64 callConnectedAtMs_ = 0;
    QString callPeer_;
    QString callId_;
    // The activity-panel operation id for the call currently in progress (so it is
    // finished when the call goes back to idle, even though the idle signal carries
    // no call id). Empty when there is no active call row.
    QString callOpId_;
    bool callMuted_ = false;
    // Rebuilds the chat list from the cached contacts.
    void rebuildChatList();
    int unreadTotal_ = 0;
    // Recomputes unreadTotal_ from the contacts model and notifies on change.
    void refreshUnreadTotal();
};

}  // namespace bazarish::app
