// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "Session.hpp"
#include "TranscriptStore.hpp"

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

class VideoPresenter;

// Holds contact-card resolutions produced off the worker thread (the slow
// federated fetch of a contact add), drained and finalized on the worker thread.
// Shared by shared_ptr with each background resolve so it outlives the worker if a
// resolve is still in flight at teardown. Defined in the .cpp.
struct ResolvedContactAddQueue;

// Runs all blocking Session work (open, subscribe, send with retries, sync) on
// a dedicated thread so the UI never freezes. Lives on that worker thread;
// commands arrive via queued calls and results leave via queued signals.
class SessionWorker : public QObject {
    Q_OBJECT
public:
    ~SessionWorker() override;

public slots:
    void openProfile(const QString& dir, const QString& passphrase);
    void connectAndSubscribe(const QStringList& facadeUrls, const QString& serverFp, int days);
    void sync();
    // Starts or stops background syncing (the account going online/offline).
    void setSyncEnabled(bool on);
    void sendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void sendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId, qint64 ttlSeconds, int downloadCount, const QString& replyTo);
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
    void saveAttachment(const QString& ref, const QString& key, const QString& destPath, qint64 token);
    void exportProfile(const QString& path, const QString& password);
    // Per-user I2P destination: set up the master (generate or load a .dat),
    // turn the paid option on/off, and report the current status.
    void generatePersonalKey();
    void loadPersonalKey(const QString& path);
    void deletePersonalKey();
    void enablePersonalDest();
    void disablePersonalDest();
    void refreshI2pStatus();
    // Polls the user's own storage usage (mailbox + blob backends) and reports it.
    void refreshStorageUsage();
    // Calls: each runs the matching Session method (strict I2P, so a failure
    // surfaces as actionFailed) and then re-emits the call state. video selects
    // an audio+video call.
    void startCall(const QString& peer, bool video);
    void acceptCall(const QString& callId);
    void declineCall(const QString& callId);
    void endCall();
    void setCallMuted(bool muted);
    void setCameraEnabled(bool enabled);
    // The GUI-thread presenters the video backend renders into; set before the
    // first openProfile so the injected backend can reach them.
    void setVideoPresenters(VideoPresenter* local, VideoPresenter* remote);

signals:
    // General background-activity stream: every observable worker operation - a
    // contact request, a reaction, a read receipt, an incoming-mail pull - opens
    // with opBegin and closes with opDone, so the activity panel shows one uniform,
    // responsive row per operation.
    // (Message/file sends and calls keep their richer dedicated rows.)
    void opBegin(const QString& opId, const QString& kind, const QString& title,
        const QString& status);
    void opDone(const QString& opId, bool ok, const QString& status);
    void opened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& subscriptionText);
    // The account's own display name was changed (so the GUI updates it without a
    // full re-open).
    void renamed(const QString& newName);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& subscriptionText);
    void messageReceived(const QVariantMap& message);
    // The current contacts, their local display names, and per-contact "1"/"0"
    // pending flags (a contact we received a request from but have not yet
    // accepted) - all parallel lists.
    void contactsRefreshed(const QStringList& fingerprints, const QStringList& names,
        const QStringList& pending);
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
    void inviteReady(const QString& uri);
    // The signed login blob for a challenge (sign-in-with-key result).
    void loginSigned(const QString& blob);
    // Whether the last sync reached the facade (true) or failed (false).
    void syncReachable(bool ok);
    // The facade currently in use, the configured facade list, and the server
    // fingerprint, for the GUI.
    void facadeInfo(
        const QString& activeUrl, const QStringList& configured, const QString& serverFp);
    // hasKey: a master is set up in the profile. enabled/active: the paid option
    // is on / currently paid-active. address: the personal b32 (empty if none).
    // summary: a one-line human status for the settings page. paidThrough: the
    // unix second the current term is paid through (0 when inactive).
    void i2pStatus(bool hasKey, bool enabled, bool active, const QString& address,
        const QString& summary, qint64 paidThrough);
    // The user's storage usage (mailbox + blob), each with an `ok` flag (a backend
    // that did not answer keeps its last figures and is marked stale by the UI).
    void storageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota,
        bool blobOk, qulonglong blobUsed, qulonglong blobQuota);
    // The serving server's onboarding info, shown when a connect/subscribe is
    // refused because this key is not registered: the refusal reason, the
    // server's message and its registration link(s).
    void serverHello(const QString& reason, const QString& message, const QStringList& links);
    // Call lifecycle: state is 0 idle / 1 outgoing / 2 incoming / 3 active,
    // matching Session::CallState. Emitted after every sync and call action.
    void callStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        bool video, bool cameraOn);

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
    VideoPresenter* localPreview_ = nullptr;
    VideoPresenter* remotePreview_ = nullptr;
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
    Q_PROPERTY(QString subscriptionText READ subscriptionText NOTIFY connectedChanged)
    // online: this account is syncing in the background (receiving). reachable:
    // the last sync actually reached the facade. Together they give the live
    // connection status shown in the account list.
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool reachable READ reachable NOTIFY reachableChanged)
    // The facade the transport is connected/connecting through, and the full
    // configured facade list (for the connection editor and status display).
    Q_PROPERTY(QString activeFacade READ activeFacade NOTIFY facadeInfoChanged)
    Q_PROPERTY(QStringList configuredFacades READ configuredFacades NOTIFY facadeInfoChanged)
    // Whether any configured facade is an I2P facade (host ends in ".b32.i2p"). When
    // full privacy mode is on and this is false, the profile cannot reach its server
    // (clearnet is refused), so its status reads as an explicit I2P-only offline error.
    Q_PROPERTY(bool hasI2pFacade READ hasI2pFacade NOTIFY facadeInfoChanged)
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
    // Unix second the personal destination is paid through (0 when inactive), so
    // the settings page can show an expiry date or the phrase "Inactive".
    Q_PROPERTY(qint64 i2pPaidThrough READ i2pPaidThrough NOTIFY i2pStatusChanged)
    // This profile's storage usage for the settings view: a map with mailboxOk,
    // mailboxUsed, mailboxQuota, blobOk, blobUsed, blobQuota (bytes), updatedAt (the
    // unix-ms time it was last fetched, 0 if never) and everFetched. The figures
    // persist for the session, so an offline profile still shows its last-known
    // usage with a "updated N ago" age.
    Q_PROPERTY(QVariantMap storageInfo READ storageInfo NOTIFY storageChanged)
    // Audio call state for the call screen: "idle"/"outgoing"/"incoming"/"active",
    // the peer fingerprint, a display name, and the local mute flag.
    Q_PROPERTY(QString callState READ callState NOTIFY callChanged)
    Q_PROPERTY(QString callPeer READ callPeer NOTIFY callChanged)
    Q_PROPERTY(QString callPeerName READ callPeerName NOTIFY callChanged)
    Q_PROPERTY(bool callMuted READ callMuted NOTIFY callChanged)
    Q_PROPERTY(bool callVideo READ callVideo NOTIFY callChanged)
    Q_PROPERTY(bool callCameraOn READ callCameraOn NOTIFY callChanged)
    // The video presenters QML binds VideoOutput.videoSink into (local self-view
    // and the remote peer). Stable for the controller's lifetime.
    Q_PROPERTY(QObject* localVideo READ localVideo CONSTANT)
    Q_PROPERTY(QObject* remoteVideo READ remoteVideo CONSTANT)
public:
    explicit SessionController(QObject* parent = nullptr);
    ~SessionController() override;

    QString fingerprint() const { return fingerprint_; }
    QString displayName() const { return displayName_; }
    bool connected() const { return connected_; }
    QString subscriptionText() const { return subscriptionText_; }
    bool online() const { return online_; }
    bool reachable() const { return reachable_; }
    QString activeFacade() const { return activeFacade_; }
    QStringList configuredFacades() const { return configuredFacades_; }
    bool hasI2pFacade() const;
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
    QVariantMap storageInfo() const;
    qint64 i2pPaidThrough() const { return i2pPaidThrough_; }
    QString callState() const { return callState_; }
    QString callPeer() const { return callPeer_; }
    QString callPeerName() const { return peerName(callPeer_); }
    bool callMuted() const { return callMuted_; }
    bool callVideo() const { return callVideo_; }
    bool callCameraOn() const { return callCameraOn_; }
    QObject* localVideo() const;
    QObject* remoteVideo() const;

    // Opens a profile on the worker thread (dir + id + passphrase).
    void open(const QString& dir, const QString& profileId, const QString& passphrase);

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
    Q_INVOKABLE void openConversation(const QString& peer);
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
    // Sends a picked file with a blob-retention choice: ttlSeconds is the TTL
    // backstop (0 -> the store default); downloadCount > 0 deletes the blob after
    // that many recipient downloads (0 -> TTL only).
    Q_INVOKABLE void sendFile(const QString& fileUrl, qint64 ttlSeconds, int downloadCount);
    // Re-dispatches a failed outgoing file. Re-uploads from the saved source path
    // (reusing the bubble); if that file is gone, emits resendFilePickRequested so
    // the UI can offer to pick a file to send instead.
    Q_INVOKABLE void resendFile(qint64 localId, const QString& protocolId);
    // A display name for any peer.
    Q_INVOKABLE QString peerName(const QString& id) const;
    // The contact's stored local display name, empty when unnamed (so a rename
    // field can prefill it and show a fingerprint placeholder otherwise).
    Q_INVOKABLE QString contactName(const QString& fp) const;
    // Sets the account's own avatar from a picked image file (file:// URL).
    Q_INVOKABLE void setAvatar(const QString& fileUrl);
    // Changes the account's own display name (trimmed). Local only: updates this
    // device and the name carried in future invite descriptors; contacts are not
    // told (each keeps their own local name for us).
    Q_INVOKABLE void setDisplayName(const QString& name);
    // --- Reactions + read receipts ---
    // Sets our reaction emoji on a message (by its protocol id) in the active chat:
    // optimistic local store + send. Tapping the emoji we already set removes it.
    Q_INVOKABLE void react(const QString& protocolId, const QString& emoji);
    // Our current reaction emoji on a message (empty when none) - for the toggle.
    Q_INVOKABLE QString myReaction(const QString& protocolId) const;
    // The reaction chips for a message: a list of { emoji, count, mine } aggregated
    // across reactors, in first-seen order.
    Q_INVOKABLE QVariantList reactionSummary(const QString& protocolId) const;
    // Renames a contact locally (mirrored to the account's own other devices).
    Q_INVOKABLE void renameContact(const QString& fp, const QString& name);
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
    Q_INVOKABLE void saveAttachment(const QString& ref, const QString& key, const QString& fileUrl);
    // Saves a received attachment to the file the user picked in the native Save
    // dialog (which already resolved any name conflict), reporting byte progress
    // and the outcome back onto the message identified by token.
    Q_INVOKABLE void saveAttachmentToFile(const QString& ref, const QString& key,
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
    Q_INVOKABLE void enablePersonalDest();
    Q_INVOKABLE void disablePersonalDest();
    Q_INVOKABLE void refreshI2pStatus();
    // Triggers a fresh poll of this profile's storage usage (mailbox + blob). The
    // result lands in the storageInfo property; until it does, the last figures (if
    // any) stay, with the UI showing how long ago they were taken.
    Q_INVOKABLE void refreshStorageUsage();
    // Audio calls. startCall dials the active/given peer; accept/decline act on
    // the current incoming call; end hangs up; setCallMuted toggles the mic.
    Q_INVOKABLE void startCall(const QString& peer);
    Q_INVOKABLE void startVideoCall(const QString& peer);
    Q_INVOKABLE void acceptCall();
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void endCall();
    Q_INVOKABLE void setCallMuted(bool muted);
    Q_INVOKABLE void setCameraEnabled(bool enabled);

signals:
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
    void sendReceiptsChanged();
    void editingChanged();
    void replyingChanged();
    void contactsRevisionChanged();
    void reactionsRevisionChanged();
    void unreadTotalChanged();
    void operationsChanged();
    void onlineChanged();
    void reachableChanged();
    void i2pStatusChanged();
    void storageChanged();
    void callChanged();
    void openFailed(const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);
    void loginSigned(const QString& blob);
    // A failed file's saved source is gone: the UI should offer to pick a file.
    void resendFilePickRequested();
    // Forwarded onboarding info for the hello dialog (unregistered-key connect).
    void serverHello(const QString& reason, const QString& message, const QStringList& links);

signals:  // to worker
    void requestConnect(const QStringList& facadeUrls, const QString& serverFp, int days);
    void requestSendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId, const QString& replyTo);
    void requestSendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId, qint64 ttlSeconds, int downloadCount, const QString& replyTo);
    void requestSendReceipt(const QString& peer, const QString& refId);
    void requestAckPending(const QString& pendingId);
    void requestSendReaction(const QString& peer, const QString& refId, const QString& emoji);
    void requestSendCallback(const QString& peer, const QString& data, const QString& ref);
    void requestSendCommand(const QString& peer, const QString& command, const QString& args);
    void requestSendEdit(const QString& peer, const QString& refId, qint64 localId,
        const QString& text);
    void requestSendDelete(const QString& peer, const QString& refId);
    void requestSetAvatar(const QString& localPath);
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
    void requestGeneratePersonalKey();
    void requestLoadPersonalKey(const QString& path);
    void requestDeletePersonalKey();
    void requestEnablePersonalDest();
    void requestDisablePersonalDest();
    void requestRefreshI2pStatus();
    void requestRefreshStorageUsage();
    void requestStartCall(const QString& peer, bool video);
    void requestAcceptCall(const QString& callId);
    void requestDeclineCall(const QString& callId);
    void requestEndCall();
    void requestSetCallMuted(bool muted);
    void requestSetCameraEnabled(bool enabled);

private slots:
    void onOpened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& subscriptionText);
    void onConnectionChanged(bool connected, const QString& subscriptionText);
    void onMessageReceived(const QVariantMap& message);
    // Connected to messageReceived AFTER onMessageReceived, so it runs once that has
    // durably stored/handled the item: acks the pending mailbox item (deferred ack),
    // so a crash between fetch and store never loses a message.
    void ackAfterReceive(const QVariantMap& message);
    void onAvatarReady(const QString& fingerprint, const QByteArray& data);
    void onContactAddStage(const QString& opId, const QString& status);
    void onContactAddDone(const QString& opId, bool ok, const QString& status);
    void onOpBegin(const QString& opId, const QString& kind, const QString& title,
        const QString& status);
    void onOpDone(const QString& opId, bool ok, const QString& status);
    void onSendProgress(qint64 localId, int state);
    void onUploadProgress(qint64 localId, qint64 sent, qint64 total);
    void onDownloadProgress(qint64 token, qint64 received, qint64 total);
    void onDownloadStage(qint64 token, int stage);
    void onDownloadFinished(qint64 token, bool ok, const QString& error);
    void onSendResult(qint64 localId, bool ok, const QString& error);
    void onSendSettled(qint64 localId, const QString& note);
    void onSendPhase(qint64 localId, const QString& phase);
    void onContactRequestSent(const QString& fingerprint, const QString& intro);
    void onSyncReachable(bool ok);
    void onFacadeInfo(
        const QString& activeUrl, const QStringList& configured, const QString& serverFp);
    void onI2pStatus(bool hasKey, bool enabled, bool active, const QString& address,
        const QString& summary, qint64 paidThrough);
    void onStorageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota,
        bool blobOk, qulonglong blobUsed, qulonglong blobQuota);
    void onCallStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        bool video, bool cameraOn);

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
        const QString& status, const QString& peer = {});
    void updateOperation(const QString& id, const QString& status, const QString& detail = {},
        double progress = -1.0);
    void finishOperation(const QString& id, bool ok, const QString& finalStatus);

    QString profileId_;
    QString fingerprint_;
    QString displayName_;
    bool connected_ = false;
    bool online_ = false;
    bool reachable_ = false;
    QString subscriptionText_;
    QString activePeer_;
    QString activeFacade_;
    QStringList configuredFacades_;
    QString serverFp_;
    bool sendReceipts_ = true;
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
    // Destination chosen for an in-flight attachment save (message id -> path),
    // recorded as the saved location once the download succeeds.
    QHash<qint64, QString> pendingSavePath_;
    // The blob-retention chosen for each outgoing file (by local id), so a resend
    // reuses the same TTL / download cap. Session-only; a resend after a restart
    // falls back to the store default.
    struct FileRetention { qint64 ttlSeconds = 0; int downloadCount = 0; };
    QHash<qint64, FileRetention> fileRetention_;
    bool i2pHasKey_ = false;
    bool i2pEnabled_ = false;
    bool i2pActive_ = false;
    QString i2pAddress_;
    QString i2pStatusText_;
    qint64 i2pPaidThrough_ = 0;
    // Last-fetched storage usage (session-scoped), with the wall-clock ms it was
    // taken so the settings view can show "updated N ago" even while offline.
    bool storageMailboxOk_ = false;
    quint64 storageMailboxUsed_ = 0;
    quint64 storageMailboxQuota_ = 0;
    bool storageBlobOk_ = false;
    quint64 storageBlobUsed_ = 0;
    quint64 storageBlobQuota_ = 0;
    qint64 storageUpdatedAtMs_ = 0;
    QString callState_ = QStringLiteral("idle");
    QString callPeer_;
    QString callId_;
    // The activity-panel operation id for the call currently in progress (so it is
    // finished when the call goes back to idle, even though the idle signal carries
    // no call id). Empty when there is no active call row.
    QString callOpId_;
    bool callMuted_ = false;
    bool callVideo_ = false;
    bool callCameraOn_ = true;
    VideoPresenter* localVideo_ = nullptr;
    VideoPresenter* remoteVideo_ = nullptr;
    // Rebuilds the chat list from the cached contacts.
    void rebuildChatList();
    int unreadTotal_ = 0;
    // Recomputes unreadTotal_ from the contacts model and notifies on change.
    void refreshUnreadTotal();
};

}  // namespace bazarish::app
