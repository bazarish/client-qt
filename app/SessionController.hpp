// Bazarish project (c) 2026
#pragma once

#include "SessionWorker.hpp"

namespace bazarish::app {

// QML-facing facade: owns the worker thread, the models and the transcript
// store; exposes invokable commands and observable properties.
class SessionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY identityChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY identityChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
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
    // The microphone is open and its level is being shown, with nothing kept:
    // what the recorder window does before the user presses Record.
    Q_PROPERTY(bool voiceMonitoring READ voiceMonitoring NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voiceElapsedMs READ voiceElapsedMs NOTIFY voiceChanged)
    // How far into the message being played back we are, in its own time; the
    // bubble fills its waveform up to here.
    Q_PROPERTY(qint64 voicePositionMs READ voicePositionMs NOTIFY voiceChanged)
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
    // Just the host of the active facade: what the status line shows, where the
    // scheme and base path only cost characters of an already long b32 name.
    Q_PROPERTY(QString activeFacadeHost READ activeFacadeHost NOTIFY facadeInfoChanged)
    Q_PROPERTY(QStringList configuredFacades READ configuredFacades NOTIFY facadeInfoChanged)
    // Whether any configured facade is an I2P facade (host ends in ".b32.i2p"). When
    // full privacy mode is on and this is false, the account cannot reach its server
    // (clearnet is refused), so its status reads as an explicit I2P-only offline error.
    // Connect-in-flight state for the connect screen: whether a connect is
    // running, what it is doing, and why the last one failed.
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectStateChanged)
    Q_PROPERTY(QString connectPhase READ connectPhase NOTIFY connectStateChanged)
    Q_PROPERTY(int connectPercent READ connectPercent NOTIFY connectStateChanged)
    Q_PROPERTY(QString connectError READ connectError NOTIFY connectStateChanged)
    // The configured server's fingerprint, so the connection editor can prefill it.
    Q_PROPERTY(QString serverFingerprint READ serverFingerprint NOTIFY facadeInfoChanged)
    // The reseeds the configured endpoint carries. The editor prefills them too:
    // it submits what it holds, and what it does not hold it would erase.
    Q_PROPERTY(QStringList configuredReseeds READ configuredReseeds NOTIFY facadeInfoChanged)
    Q_PROPERTY(QString activePeer READ activePeer NOTIFY activePeerChanged)
    // The active peer's display name (the local label, or a short fingerprint when
    // unnamed). Notified on both opening a conversation and a rename, so the chat
    // header stays current.
    Q_PROPERTY(QString activePeerName READ activePeerName NOTIFY activePeerNameChanged)
    // Paging state of the open conversation: whether the newest page is loaded
    // (so stick-to-bottom applies) and whether older history remains above.
    Q_PROPERTY(bool atNewest READ atNewest NOTIFY pagingChanged)
    Q_PROPERTY(bool hasMoreOlder READ hasMoreOlder NOTIFY pagingChanged)
    // The on-disk account id this session was opened from (stable per account).
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
    // Whether this account takes incoming calls. Off, a caller is refused at once
    // instead of ringing; their call button stays, because this can be turned back
    // on at any moment. Kept with the account, not with the window.
    Q_PROPERTY(bool acceptCalls READ acceptCalls WRITE setAcceptCalls NOTIFY acceptCallsChanged)
    // Whether contacts are handed the capability that lets them pass us on.
    Q_PROPERTY(bool sharingAllowed READ sharingAllowed WRITE setSharingAllowed
            NOTIFY sharingAllowedChanged)
    // The rotation in progress: what it is doing, and whether one is running.
    Q_PROPERTY(QString servingKeyStage READ servingKeyStage NOTIFY servingKeyChanged)
    Q_PROPERTY(bool servingKeyBusy READ servingKeyBusy NOTIFY servingKeyChanged)
    // The names this account holds in the central registry, a row per alias for
    // the settings table, and the line said under it. Empty until the user
    // activates servicing: until then this client has asked the name service
    // nothing.
    Q_PROPERTY(QVariantList aliasHoldings READ aliasHoldings NOTIFY aliasChanged)
    Q_PROPERTY(QString aliasNote READ aliasNote NOTIFY aliasChanged)
    Q_PROPERTY(bool aliasBusy READ aliasBusy NOTIFY aliasChanged)
    // How long this account hands its destination to the server for, in days.
    // Shorter means leaving a server takes effect sooner; longer means a client
    // that is away stays reachable. Bounded by the protocol, not by the server.
    Q_PROPERTY(int delegationDays READ delegationDays WRITE setDelegationDays
            NOTIFY delegationDaysChanged)
    Q_PROPERTY(int minDelegationDays READ minDelegationDays CONSTANT)
    Q_PROPERTY(int maxDelegationDays READ maxDelegationDays CONSTANT)
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
    // What the server serves, and whether it is something other than the address
    // this device holds the keys for. A mismatch means mail sent to the address
    // this device shows never arrives, which the window has to say out loud.
    Q_PROPERTY(QString i2pServedAddress READ i2pServedAddress NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pAddressMismatch READ i2pAddressMismatch NOTIFY i2pStatusChanged)
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
    // This account's storage usage for the settings view: a map with mailboxOk,
    // mailboxUsed, mailboxQuota (bytes), updatedAt (the
    // unix-ms time it was last fetched, 0 if never) and everFetched. The figures
    // persist for the session, so an offline account still shows its last-known
    // usage with a "updated N ago" age.
    Q_PROPERTY(QVariantMap storageInfo READ storageInfo NOTIFY storageChanged)
    // What this account holds on THIS device, for the storage window: a map with
    // busy, measuredAt (unix ms, 0 when never), fileBytes, freeBytes and chats -
    // each of them peer, name, messages, bytes and mediaCount, heaviest first.
    // Content bytes rather than pages: the file is larger than their sum.
    Q_PROPERTY(QVariantMap deviceStorage READ deviceStorage NOTIFY deviceStorageChanged)
    // The two depths a conversation can be trimmed to, so the window's labels and
    // the statements behind them name the same numbers.
    Q_PROPERTY(int keepRecentMessages READ keepRecentMessages CONSTANT)
    Q_PROPERTY(int keepManyMessages READ keepManyMessages CONSTANT)
    // Audio call state for the call screen: "idle"/"outgoing"/"incoming"/"active",
    // the peer fingerprint, a display name, and the local mute flag.
    Q_PROPERTY(QString callState READ callState NOTIFY callChanged)
    // What the caller is waiting on (delivering the invitation, ringing, opening
    // the audio path); empty once the call is running. And when it started.
    Q_PROPERTY(QString callStage READ callStage NOTIFY callChanged)
    Q_PROPERTY(qint64 callConnectedAtMs READ callConnectedAtMs NOTIFY callChanged)
    // Live loudness in each direction, 0..1: the call window draws both, so a
    // silent call still shows whether the microphone works and whether anything
    // is arriving from the other side.
    Q_PROPERTY(qreal callInputLevel READ callInputLevel NOTIFY callLevelsChanged)
    Q_PROPERTY(qreal callOutputLevel READ callOutputLevel NOTIFY callLevelsChanged)
    Q_PROPERTY(QString callPeer READ callPeer NOTIFY callChanged)
    Q_PROPERTY(QString callPeerName READ callPeerName NOTIFY callChanged)
    Q_PROPERTY(bool callMuted READ callMuted NOTIFY callChanged)
    // and the remote peer). Stable for the controller's lifetime.
public:
    explicit SessionController(QObject* parent = nullptr);
    ~SessionController() override;

    // Closes the account without blocking whoever asked. Everything this session
    // runs is stopped on its own thread - the long poll first, then the timers,
    // then the session - and closed() says when that is done. Nothing the user is
    // looking at waits for a network call to time out. Idempotent.
    void beginShutdown();
    // Stops the worker and lets go of the account's files. Blocking, and only for
    // the destructor: everywhere else the asynchronous path above is the one to
    // use. Idempotent.
    void shutdown();

    QString fingerprint() const { return fingerprint_; }
    QString displayName() const { return displayName_; }
    bool connected() const { return connected_; }
    bool online() const { return online_; }
    bool reachable() const { return reachable_; }
    QString syncError() const { return syncError_; }
    bool i2pBusy() const { return i2pBusy_; }
    QVariantList devices() const { return devices_; }
    bool voiceRecording() const { return voiceRecording_; }
    bool voiceMonitoring() const { return voiceMonitoring_; }
    // Starts and stops that watching; the recorder window owns both ends of it.
    Q_INVOKABLE void startVoiceMonitor();
    Q_INVOKABLE void stopVoiceMonitor();
    qint64 voiceElapsedMs() const { return voiceElapsedMs_; }
    qint64 voicePositionMs() const { return voicePositionMs_; }
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
    bool connecting() const { return connecting_; }
    QString connectPhase() const { return connectPhase_; }
    int connectPercent() const { return connectPercent_; }
    QString connectError() const { return connectError_; }
    QString serverFingerprint() const { return serverFp_; }
    QStringList configuredReseeds() const { return configuredReseeds_; }
    QString activePeer() const { return activePeer_; }
    QString activePeerName() const { return peerName(activePeer_); }
    bool atNewest() const;
    bool hasMoreOlder() const;
    QString accountId() const { return accountId_; }
    int unreadTotal() const { return unreadTotal_; }
    QObject* contacts() { return &contacts_; }
    QObject* chatList() { return &contactsProxy_; }
    // Filters the chat list by name (case-insensitive substring); empty shows all.
    Q_INVOKABLE void setChatFilter(const QString& text);
    // Pin/unpin a chat to the top of the list, synced to the account's other
    // devices. The row reads its own state from the model.
    Q_INVOKABLE void pinChat(const QString& peer, bool pinned);
    QObject* conversation() { return &conversation_; }
    QObject* operations() { return &operations_; }
    int activeOperations() const { return operations_.runningCount(); }
    bool acceptCalls() const { return acceptCalls_; }
    bool sharingAllowed() const { return sharingAllowed_; }
    void setSharingAllowed(bool allowed);
    QString servingKeyStage() const { return servingKeyStage_; }
    bool servingKeyBusy() const { return servingKeyBusy_; }
    // Rotates the serving key our server holds and the capability that reads our
    // card, and hands both to every contact. Reported step by step.
    Q_INVOKABLE void rotateServingKey();
    QVariantList aliasHoldings() const { return aliasHoldings_; }
    QString aliasNote() const { return aliasNote_; }
    bool aliasBusy() const { return aliasBusy_; }
    // Asks the name service which names this account holds and starts servicing
    // them. The one way in: with no name known this client never asks on its own.
    Q_INVOKABLE void activateAliasServicing();
    int delegationDays() const { return delegationDays_; }
    static int minDelegationDays() { return static_cast<int>(bazarish::kMinDelegationDays); }
    static int maxDelegationDays() { return static_cast<int>(bazarish::kMaxDelegationDays); }
    void setDelegationDays(int days);
    void setAcceptCalls(bool on);
    bool sendReceipts() const { return sendReceipts_; }
    // Kept in the account, not in this window: it is the account's own answer, and
    // it reaches the account's other devices.
    void setSendReceipts(bool on);
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
    QString i2pServedAddress() const { return i2pServedAddress_; }
    bool i2pAddressMismatch() const
    {
        return !i2pServedAddress_.isEmpty() && !i2pAddress_.isEmpty()
            && i2pServedAddress_ != i2pAddress_;
    }
    QString i2pStatusText() const { return i2pStatusText_; }
    QString i2pServerState() const { return i2pServerState_; }
    QString acceptingContact() const { return acceptingContact_; }
    QVariantMap storageInfo() const;
    QVariantMap deviceStorage() const { return deviceStorage_; }
    int keepRecentMessages() const { return kKeepRecentMessages; }
    int keepManyMessages() const { return kKeepManyMessages; }
    qint64 i2pTransientExpires() const { return i2pTransientExpires_; }
    QString callState() const { return callState_; }
    QString callStage() const { return callStage_; }
    qint64 callConnectedAtMs() const { return callConnectedAtMs_; }
    qreal callInputLevel() const { return callInputLevel_; }
    qreal callOutputLevel() const { return callOutputLevel_; }
    QString callPeer() const { return callPeer_; }
    QString callPeerName() const { return peerName(callPeer_); }
    bool callMuted() const { return callMuted_; }

    // Opens an account on the worker thread (dir + id + passphrase).
    void open(const QString& file, const QString& accountId, const QString& passphrase,
        bool startOnline = true);

    // Connects (and subscribes) through an ordered list of facade URLs
    // (http[s]://host[:port][/secret]). The client fails over across them.
    Q_INVOKABLE void connectServer(const QStringList& facadeUrls, const QString& serverFp,
        const QStringList& reseedUrls = {});
    // Decodes a bazarish://server/... link into { serverFp, facades } for the
    // connect form to prefill; returns an empty map on a malformed link.
    Q_INVOKABLE QVariantMap parseServerLink(const QString& uri) const;
    // Brings this account online (resume syncing) or offline (stop syncing
    // without unloading it).
    void goOnline();
    void goOffline();
    // Drops the I2P destinations this account holds so they are built again with
    // the tunnel profile now in force.
    void rebuildI2pLinks();
    Q_INVOKABLE void openConversation(const QString& peer);
    // Leaves the open conversation without opening another: on a narrow window
    // the chat is the whole window, so there has to be a way back to the list.
    Q_INVOKABLE void closeConversation();
    // Opens a conversation positioned at a specific message (a search hit): loads
    // a window ending at it and asks the view to scroll there.
    Q_INVOKABLE void openConversationAtMessage(const QString& peer, qint64 localId);
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
    // Sends text a message offered as one tap - a bot's command, written into it
    // between the send markers. The composer is not touched: a draft being
    // written and a reply being aimed at a message both stay where they are.
    Q_INVOKABLE void sendOffered(const QString& text);
    // Passes a message this account holds on to another chat as a message of its
    // own, marked forwarded. The mark says only that: it names nobody and proves
    // nothing about who wrote what it carries. Text, pictures and voice travel;
    // a file only when this device still has the bytes.
    Q_INVOKABLE void forwardMessage(const QString& e2eId, const QString& toPeer);
    // Re-dispatches a failed outgoing text message (same protocol id) after the
    // user taps "Resend" on its bubble.
    Q_INVOKABLE void resendText(qint64 localId, const QString& text, const QString& e2eId);
    // Sends a picked file. Nothing to choose: the bytes go device to device over a
    // one-time destination, so there is no store to keep them in and no retention
    // to set; only the offer travels through the servers.
    Q_INVOKABLE void sendFile(const QString& fileUrl);
    // The same transfer announced as a picture: the recipient fetches and shows
    // it instead of being offered a Save button.
    Q_INVOKABLE void sendPicture(const QString& fileUrl);
    // Writes a picture this account holds out to a file the user chose.
    Q_INVOKABLE void savePictureAs(const QString& e2eId, const QString& fileUrl);
    // Puts it on the clipboard as an image: it goes from memory to memory, and
    // never becomes a plaintext file on the way.
    Q_INVOKABLE void copyPicture(const QString& e2eId);

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
    void stopVoiceTake();
    Q_INVOKABLE void sendVoiceTake();
    Q_INVOKABLE void discardVoiceTake();
    // Plays a voice message. fromMs of -1 means "the play button": start from
    // the beginning, or stop if this message is the one already playing. A real
    // position means the waveform was tapped there, and playback moves to it.
    Q_INVOKABLE void playVoice(const QString& e2eId, qint64 fromMs = -1);
    void stopVoice();
    // Steps the playback speed through the offered rates and back to normal.
    Q_INVOKABLE void cycleVoiceSpeed();
    // A name to suggest for that file.
    Q_INVOKABLE QUrl defaultPictureSaveUrl(const QString& e2eId, const QString& name) const;
    // Re-dispatches a failed outgoing file from the saved source path (reusing the
    // bubble); if that file is gone, emits resendFilePickRequested so the UI can
    // offer to pick a file to send instead.
    Q_INVOKABLE void resendFile(qint64 localId, const QString& e2eId);
    // Sends a failed voice take again from the recording kept in the account.
    Q_INVOKABLE void resendVoice(qint64 localId, const QString& e2eId);
    // A display name for any peer.
    Q_INVOKABLE QString peerName(const QString& id) const;
    // The saved-messages chat: our own fingerprint, and the name it goes by. Not
    // a contact, never deletable, and the same chat on every device.
    QString savedPeer() const;
    static QString savedChatName();
    Q_INVOKABLE bool isSavedChat(const QString& peer) const
    {
        return !peer.isEmpty() && peer == savedPeer();
    }
    // Empties it here and on every other device of this account.
    Q_INVOKABLE void clearSavedEverywhere();

    // --- Blocking and per-contact switches ---
    Q_INVOKABLE bool isBlocked(const QString& peer) const;
    Q_INVOKABLE void setBlocked(const QString& peer, bool blocked);
    // Blocked correspondents, newest first: [{fingerprint, name}].
    Q_INVOKABLE QVariantList blockedList() const;
    Q_INVOKABLE bool contactNotifications(const QString& peer) const;
    Q_INVOKABLE bool contactCalls(const QString& peer) const;
    Q_INVOKABLE void setContactNotifications(const QString& peer, bool on);
    Q_INVOKABLE void setContactCalls(const QString& peer, bool allowed);
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
    Q_INVOKABLE void react(const QString& e2eId, const QString& emoji);
    // Reactions this user reached for that are not in the standard set, newest
    // first. Kept per account so the picker offers what this person actually uses.
    // The set the picker offers by default. Held here because it also decides
    // what counts as "one of this user's own" for the recents below, and that
    // decision has to be the same wherever a reaction is set from.
    Q_PROPERTY(QStringList standardReactions READ standardReactions CONSTANT)
    QStringList standardReactions() const;
    Q_PROPERTY(QStringList recentReactions READ recentReactions NOTIFY recentReactionsChanged)
    QStringList recentReactions() const { return recentReactions_; }
    void rememberReaction(const QString& emoji);
    // Our current reaction emoji on a message (empty when none) - for the toggle.
    QString myReaction(const QString& e2eId) const;
    // The reaction chips for a message: a list of { emoji, count, mine } aggregated
    // across reactors, in first-seen order.
    Q_INVOKABLE QVariantList reactionSummary(const QString& e2eId) const;
    // Renames a contact locally (mirrored to the account's own other devices).
    Q_INVOKABLE void renameContact(const QString& fp, const QString& name);
    // A shareable link for a contact we hold: the same artifact as our own
    // invite, built from what they already gave us. Empty while we hold no
    // routing for them.
    Q_INVOKABLE QString contactInvite(const QString& fp) const;
    // Whether this contact has said their invite may not be passed on, which is
    // a different thing from not having sent us one yet.
    Q_INVOKABLE bool contactSharingRefused(const QString& fp) const
    {
        return contactState_.value(fp).sharingRefused;
    }
    // Whether this device holds the pass that admits it to a contact's mailbox.
    Q_INVOKABLE bool canWriteTo(const QString& fp) const;
    // Our own invite, from what this account already holds: the fingerprint and
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
    // Sends the contact request of the active chat again, as the same request.
    // What the note over it offers when an add ended with nothing.
    Q_INVOKABLE void retryContactAdd();
    // Inline-keyboard button presses in the active conversation: a callback
    // (button data + the keyboard message's protocol id) or a command button.
    // A button press. The label is passed only so the activity panel can name
    // what is in flight - the wire carries the payload, not the label.
    Q_INVOKABLE void sendCallback(
        const QString& data, const QString& refMsgId, const QString& label = {});
    Q_INVOKABLE void sendCommand(
        const QString& command, const QString& args, const QString& label = {});
    // Editing one's own message: start (prefilling the composer), commit the new
    // text (updates our copy and sends an edit to the peer), or cancel.
    Q_INVOKABLE void beginEdit(qint64 localId, const QString& e2eId, const QString& text);
    Q_INVOKABLE void commitEdit(const QString& newText);
    Q_INVOKABLE void cancelEdit();
    // Reply: start replying to a message (the composer shows a quote banner), or
    // cancel. The next sent message carries the referenced protocol id.
    Q_INVOKABLE void beginReply(
        const QString& e2eId, const QString& previewText, const QString& sender);
    Q_INVOKABLE void cancelReply();
    // Resolves a reply reference to the original message in the open conversation:
    // { found, localId, text, sender } so a bubble can render a clickable quote.
    Q_INVOKABLE QVariantMap replyPreview(const QString& e2eId) const;
    // Deletes a message with no trace. The local copy is always removed; for one's
    // own one-to-one message (outgoing, e2eId set) the recipient is asked to
    // remove its copy too. A received message is removed locally only.
    Q_INVOKABLE void deleteMessage(qint64 localId, const QString& e2eId, bool outgoing);
    // Copies arbitrary text (a whole message) to the system clipboard.
    Q_INVOKABLE void copyText(const QString& text) const;
    // Why this text is not a usable invite, or empty when it parses. Local and
    // instant: a paste that cannot work must be refused at the field, not by a
    // background operation that dials I2P first.
    Q_INVOKABLE QString inviteProblem(const QString& uri) const;
    Q_INVOKABLE void addByInvite(const QString& uri, const QString& intro);
    // The same add, repeating a request that already has a name.
    void addByInvite(const QString& uri, const QString& intro, const QString& requestId);
    // Sends a contact request again after the recipient's address refused it for
    // being over its cap. Called by the timer that repeats it, and by the user
    // from the chat once the automatic tries are spent.
    Q_INVOKABLE void retryContactRequest(const QString& fingerprint);
    Q_INVOKABLE void addByAlias(const QString& alias, const QString& intro);
    // Agrees to the active chat's received contact request (the green "Agree").
    Q_INVOKABLE void acceptContact();
    // Whether `fp` is a contact that sent us a request we have not yet accepted
    // (drives the "Agree" button on an incoming contact-request bubble).
    Q_INVOKABLE bool contactCanAccept(const QString& fp) const;
    // Whether this contact's acceptance is in the air: agreed to, not yet
    // confirmed stored by their server.
    Q_INVOKABLE bool contactAgreeing(const QString& fp) const;
    // The reactions on this message that arrived while nobody was looking at
    // them, as a list of emoji. The conversation flashes them once and then asks
    // for them to be forgotten. Re-read whenever reactionsRevision changes.
    Q_INVOKABLE QStringList reactionsToFlash(const QString& e2eId) const;
    // Stop flashing: everything pending in the open conversation has been seen.
    Q_INVOKABLE void forgetReactionFlash();
    Q_INVOKABLE void requestInvite();
    // Signs a sign-in-with-key challenge with this account's key (no server
    // needed); the result arrives via loginSigned(). The key never leaves the app.
    // Signs a portal challenge and answers with loginSigned(). Runs here, not on
    // the account's thread: it is local work, and the click must not wait for a
    // sync to end.
    Q_INVOKABLE void signLogin(const QString& challenge);
    // Who a pasted challenge says will consume the signature, for the window to
    // put in front of the user before they sign: {ok, name, place, role} or
    // {ok: false, problem}. A challenge that names nobody is not signable, so
    // this is also what disables the button.
    Q_INVOKABLE QVariantMap describeLoginChallenge(const QString& challenge) const;
    // The account's connection log: what went to the server and what came back,
    // including deliveries to correspondents and their outcome. Answered by
    // connectionLogUpdated(); kept in memory only, and short.
    // Asks this account's other devices for the address book. Automatic once on
    // a device's first sync; this is the same question by hand, for when that
    // did not reach anybody (no other device was online).
    Q_INVOKABLE void askForContacts();
    Q_INVOKABLE void refreshConnectionLog();
    Q_INVOKABLE void clearConnectionLog();
    // Saves a received attachment to the file the user picked in the native Save
    // dialog (which already resolved any name conflict), reporting byte progress
    // and the outcome back onto the message identified by token.
    // A file is fetched from the peer that announced it, by the announcing
    // message's protocol id - there is no store to fetch it from.
    Q_INVOKABLE void saveAttachmentToFile(const QString& peer, const QString& e2eId,
        const QString& fileUrl, qint64 token);
    // A suggested save location (the Downloads folder joined with fileName) as a
    // file URL, used to pre-fill the native Save dialog's name and folder.
    Q_INVOKABLE QUrl defaultSaveUrl(const QString& fileName) const;
    // Whether a saved attachment still exists on disk (drives Save vs Open).
    Q_INVOKABLE bool fileExists(const QString& path) const;
    // Reveals a saved attachment in the system file manager with the file itself
    // selected (falling back to opening its folder).
    Q_INVOKABLE void showInFolder(const QString& path) const;
    Q_INVOKABLE void exportAccount(const QString& fileUrl, const QString& password);
    // Changes the passphrase this account is kept under at rest. An empty one
    // leaves it unencrypted, which is what an account created without a
    // passphrase already is.
    Q_INVOKABLE void changePassphrase(const QString& passphrase);
    Q_INVOKABLE QString shortFingerprint(const QString& fp) const;
    // Per-user I2P destination controls (drive the worker thread).
    Q_INVOKABLE void generatePersonalKey();
    Q_INVOKABLE void loadPersonalKey(const QString& fileUrl);
    Q_INVOKABLE void deletePersonalKey();
    // Sticky I2P's escape hatch: this account has reached its server over I2P and
    // refuses clearnet since; allowing it again is the user's call, never automatic.
    // Stops a file transfer in either direction, by the file's protocol id (the
    // activity panel offers this on a running transfer).
    Q_INVOKABLE void cancelTransfer(const QString& e2eId);
    Q_INVOKABLE void publishPersonalDest();
    Q_INVOKABLE void disablePersonalDest();
    Q_INVOKABLE void refreshI2pStatus();
    // Answers to the address question above.
    Q_INVOKABLE void keepThisDeviceAddress();
    Q_INVOKABLE void useFreshAddress();
    // Triggers a fresh poll of this account's storage usage (mailbox + blob). The
    // result lands in the storageInfo property; until it does, the last figures (if
    // any) stay, with the UI showing how long ago they were taken.
    Q_INVOKABLE void refreshStorageUsage();
    // Reads what this account weighs on this device. A full pass over the
    // transcript, so it is done when the window opens and when the user asks -
    // never on a timer.
    Q_INVOKABLE void measureDeviceStorage();
    // Keeps the newest `keep` messages of one conversation, or of every one of
    // them, and rewrites the file so the space returns to the disk. Local: no
    // other device is told and nothing leaves this machine.
    Q_INVOKABLE void trimChat(const QString& peer, int keep);
    Q_INVOKABLE void trimEveryChat(int keep);
    // Rewrites the file without removing anything, so free space inside it goes
    // back to the disk. Nothing is deleted here - it is the same rewrite a trim
    // ends with, offered on its own.
    Q_INVOKABLE void compactDatabase();
    Q_INVOKABLE void refreshDevices();
    // Drops a device's registration: its unacked mail stops being held, and the
    // device registers again the next time it connects.
    Q_INVOKABLE void forgetDevice(const QString& clientId);
    // Ends this account on its server; the answer arrives as accountClosedOnServer.
    void closeAccountOnServer();
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
    void scrollToMessage(qint64 localId);
    // Asks the view to position the first unread message near the top and briefly
    // highlight the unread tail (a conversation opened with unread messages).
    void scrollToUnread(qint64 firstUnreadId);
    // Asks the view to scroll to the bottom (jump-to-latest).
    void scrollToBottom();
    // Mirrors the worker's accountClosed to whoever asked for the deletion.
    void accountClosedOnServer(bool ok, const QString& error);
    void facadeInfoChanged();
    void connectStateChanged();
    void sendReceiptsChanged();
    void acceptCallsChanged();
    void delegationDaysChanged();
    // The automatic retries of a refused contact request are spent: the chat
    // offers to send it again by hand.
    void contactRetryExhausted(const QString& fingerprint);
    void editingChanged();
    void replyingChanged();
    void contactsRevisionChanged();
    void reactionsRevisionChanged();
    void unreadTotalChanged();
    // Something arrived that a person would want to be told about, by the name it
    // came from. Everything that is not a message - receipts, refills, control
    // traffic - is filtered out before this.
    // A message arrived from `peer` (named by `fromName`). The fingerprint rides
    // with the name because a notification is something to click: what it is
    // about has to be enough to open the conversation it came from.
    void messageNotification(const QString& peer, const QString& fromName);
    // A reaction arrived on one of our messages: who reacted, and with what. Its
    // own signal because it is announced with its own, shorter sound.
    void reactionNotification(const QString& peer, const QString& fromName,
        const QString& emoji);
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
    void deviceStorageChanged();
    void callChanged();
    void callLevelsChanged();
    void openFailed(const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);
    void inviteUnavailable(const QString& reason);
    void loginSigned(const QString& blob);
    // The server serves an address this device has no keys for, and no other
    // device of this account answered with them. Two ways out, both the user's.
    void addressNeedsChoice(const QString& servedHost, const QString& ourHost);
    // The connection log, oldest first: one map per line with at/outgoing/what/
    // status/detail. Answers refreshConnectionLog().
    void connectionLogUpdated(const QVariantList& lines);
    // This session is closed: its thread has ended and nothing of it is running.
    void closed();
    // A failed file's saved source is gone: the UI should offer to pick a file.
    void resendFilePickRequested();
    // Forwarded onboarding info for the hello dialog (unregistered-key connect).
    void serverHello(const QString& reason, const QString& message, const QStringList& links);

signals:  // to worker
    void requestPublishThisDeviceAddress();
    void requestPublishFreshAddress();
    void requestConnect(const QStringList& facadeUrls, const QString& serverFp,
        const QStringList& reseedUrls);
    void requestSendText(const QString& peer, const QString& text, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void requestSendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& e2eId, const QString& replyTo);
    void requestSendPicture(const QString& peer, const QString& localPath, qint64 localId,
        const QString& e2eId, const QString& replyTo);
    void requestSendVoice(const QString& peer, const QByteArray& opus, qint64 durationMs,
        qint64 localId, const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void requestSendReceipt(const QString& peer, const QString& refId);
    void requestAckPending(const QString& pendingId);
    void requestSendReaction(const QString& peer, const QString& refId, const QString& emoji);
    void requestSendCallback(
        const QString& opId, const QString& peer, const QString& data, const QString& ref);
    void requestSendCommand(
        const QString& opId, const QString& peer, const QString& command, const QString& args);
    void requestSendEdit(const QString& peer, const QString& refId, qint64 localId,
        const QString& text);
    void requestSendDelete(const QString& peer, const QString& refId);
    // Forget a file this device announced, without telling the peer anything: a
    // message deleted only here must not leave behind the record that would
    // still serve its bytes.
    void requestUnsend(const QString& refId);
    void requestSetAvatar(const QString& localPath);
    void requestClearAvatar();
    void requestSetDisplayName(const QString& name);
    void requestRenameContact(const QString& peer, const QString& name);
    void requestRemoveContact(const QString& peer);
    void requestClearSaved();
    void requestSetBlocked(const QString& peer, bool blocked);
    void requestSetContactNotifications(const QString& peer, bool on);
    void requestSetContactCalls(const QString& peer, bool allowed);
    void requestSyncChatPin(const QString& peer, bool pinned);
    void requestSyncRead(const QString& peer, qint64 sentAtMs);
    void requestSyncChatClear(const QString& peer);
    void requestEmitSettings();
    void requestClearChatForEveryone(const QString& peer);
    void requestAddByInvite(const QString& uri, const QString& intro, const QString& opId,
        const QString& requestId);
    void requestAddByAlias(const QString& alias, const QString& intro, const QString& opId);
    void requestAcceptContact(const QString& peer);
    void requestInviteSig();
    void requestSignLoginSig(const QString& challenge);
    void requestConnectionLog();
    void requestContactsFromDevices();
    void requestShutdown();
    void requestClearConnectionLog();
    void requestSaveAttachment(const QString& ref, const QString& key, const QString& destPath,
        qint64 token);
    void requestExport(const QString& path, const QString& password);
    void requestChangePassphrase(const QString& passphrase);
    void requestRotateServingKey();
    void requestActivateAliasServicing();
    void requestSharingAllowed(bool allowed);
    void sharingAllowedChanged();
    void servingKeyChanged();
    void aliasChanged();
    void requestOpen(const QString& dir, const QString& passphrase, bool startOnline);
    void requestSetSync(bool on);
    void requestRebuildI2p();
    void requestCancelTransfer(const QString& e2eId);
    void requestGeneratePersonalKey();
    void requestLoadPersonalKey(const QString& path);
    void requestDeletePersonalKey();
    void requestSetAcceptCalls(bool accept);
    void requestSetSendReceipts(bool on);
    void requestSetDelegationDays(int days);
    void requestPublishPersonalDest();
    void requestDisablePersonalDest();
    void requestRefreshI2pStatus();
    void requestRefreshStorageUsage();
    void requestRefreshDevices();
    void requestForgetDevice(const QString& clientId);
    // An add that ended, whichever way: its intent is no longer worth keeping.
    void requestForgetPendingAdd(const QString& opId);
    void requestCloseAccountOnServer();
    void requestStartCall(const QString& peer);
    void requestAcceptCall(const QString& callId);
    void requestDeclineCall(const QString& callId);
    void requestEndCall();
    void requestSetCallMuted(bool muted);

private slots:
    // Every command sent to the worker passes through these two: one when it is
    // asked for, one when the worker has finished it. Connected to each command
    // signal by name rather than one by one, so work that touches the network
    // shows up in the activity panel whether or not anybody remembered it.
    void noteCommandQueued();
    void onCommandFinished();
    void onOpened(const QString& fingerprint, const QString& displayName, bool connected);
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
    void onContactAlreadyKnown(const QString& opId, const QString& fingerprint);
    void writeConversationNote(const QString& peer, const QString& text);
    void onContactAddRateLimited(
        const QString& opId, const QString& fingerprint, const QString& requestId);
    void onContactAccepted(const QString& peer, bool ok, const QString& reason);
    void onOpBegin(const QString& opId, const QString& kind, const QString& title,
        const QString& status);
    void onOpDone(const QString& opId, bool ok, const QString& status);
    void onSendProgress(qint64 localId, int state);
    void onUploadProgress(qint64 localId, qint64 sent, qint64 total);
    void onDownloadProgress(qint64 token, qint64 received, qint64 total);
    void onServedProgress(const QString& peer, const QString& e2eId, qint64 sent,
        qint64 total);
    void onTransferStage(const QString& peer, const QString& e2eId, const QString& stage);
    void onServedFinished(const QString& peer, const QString& e2eId, bool ok,
        const QString& error);
    void onDownloadFinished(qint64 token, bool ok, const QString& error);
    void onSendResult(qint64 localId, bool ok, const QString& error);
    void onSendPhase(qint64 localId, const QString& phase);
    void onContactRequestSent(
        const QString& fingerprint, const QString& intro, const QString& requestId);
    void onSyncReachable(bool ok, const QString& reason);
    void onApprovalState(bool pending, const QString& note);
    void onConnectProgress(int percent, const QString& phase);
    void onFacadeInfo(const QString& activeUrl, const QStringList& configured,
        const QString& serverFp, const QStringList& reseeds);
    void onI2pStatus(bool hasKey, bool enabled, bool active, const QString& address,
        const QString& summary, qint64 transientExpires, const QString& serverState);
    void onI2pKeyState(bool hasKey, const QString& address);
    void onDevicesReady(const QVariantList& devices);
    void onVoiceLoaded(const QString& e2eId, const QByteArray& bytes);
    // Pulls a small incoming picture into the media cache without being asked.
    void requestPicturesFor(const QList<StoredMessage>& messages);
    void onStorageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota);
    void onCallStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, bool peerRinging, qint64 connectedAtMs, float inputLevel,
        float outputLevel);
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
    // Commands asked of the worker and not yet finished, oldest first. The
    // worker takes them one at a time in this order, so the first is the one it
    // is on and the rest are waiting their turn.
    struct QueuedCommand {
        QString id;
        QString title;
        qint64 queuedAtMs = 0;
        // A command that is over before anybody could read it is not worth a
        // row; one that is still here after the grace gets one.
        bool shown = false;
    };
    QList<QueuedCommand> commandQueue_;
    qint64 commandSeq_ = 0;
    QTimer commandTimer_;
    void showSlowCommands();
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

    // Records an outgoing text message and hands it to the courier. What the two
    // ways of sending one share; the reply reference is what differs.
    void deliverText(const QString& text, const QString& replyTo);
    // Lifts a block because the user is writing to them. Called from the four
    // places a person composes something - a message, a file, a picture, a voice
    // note - and from nowhere automatic.
    void unblockBeforeWriting(const QString& peer);

    QString accountId_;
    QString fingerprint_;
    QString displayName_;
    bool connected_ = false;
    bool online_ = false;
    // What this account was opened as. An account opened offline stays offline
    // until the switch says otherwise, so opening it never reads as switching
    // it on.
    bool startOnline_ = true;
    bool reachable_ = false;
    QString syncError_;
    bool i2pBusy_ = false;
    bool awaitingApproval_ = false;
    QString approvalNote_;
    QString activePeer_;
    QString activeFacade_;
    bool avatarBusy_ = false;
    QStringList configuredFacades_;
    QStringList configuredReseeds_;
    QString serverFp_;
    bool sendReceipts_ = true;
    bool acceptCalls_ = true;
    bool sharingAllowed_ = true;
    QString servingKeyStage_;
    QVariantList aliasHoldings_;
    QString aliasNote_;
    bool aliasBusy_ = false;
    bool servingKeyBusy_ = false;
    int delegationDays_ = static_cast<int>(bazarish::kDefaultDelegationDays);
    // Edit-in-progress state for the composer (0 / empty when not editing).
    bool editing_ = false;
    qint64 editingLocalId_ = 0;
    QString editingE2eId_;
    QString editingText_;
    // Reply-in-progress state for the composer (empty when not replying).
    bool replying_ = false;
    QString replyingE2eId_;
    QString replyingText_;
    QString replyingSender_;
    // Contacts we received a request from but have not accepted yet (their
    // fingerprints), so a request bubble can offer "Agree". Refreshed from the
    // worker; contactsRevision_ bumps on every refresh to re-drive the binding.
    // Whose acceptance has a row in the activity panel. An acceptance that has
    // left this device and has not reached the other side is still in flight,
    // and it outlives the command that sent it.
    QSet<QString> agreeingShown_;
    void syncAgreeingRows();
    // Contacts whose acceptance this account has sent and is still waiting on.
    QStringList agreeingFingerprints() const;
    // Reactions that arrived while their conversation was not being looked at,
    // as "peer\ntarget". Kept in the account's database rather than in the
    // reactions table: that table is keyed by who reacted and has no room for
    // this, and its schema is refused when the number does not match - a column
    // here would make every account made by an earlier build unreadable.
    QStringList reactionsToFlash_;
    // Remembers one, and writes the list down.
    void noteReactionToFlash(const QString& peer, const QString& target);
    void persistReactionsToFlash();
    int contactsRevision_ = 0;
    // Bumped on any reaction change so QML re-queries the store.
    int reactionsRevision_ = 0;
    // Current delivery status per outgoing local id, so a later/lower signal
    // (e.g. "yellow" arriving after "green") never downgrades the tick.
    QHash<qint64, int> statusById_;
    void bumpStatus(qint64 localId, int status);
    // Puts a message back at the start of delivery and clears the error it is
    // showing: what an edit or a resend does before dispatching again. This is
    // the one downgrade there is, so it does not go through bumpStatus.
    void restartDelivery(qint64 localId);
    // Records an outgoing attachment and opens its activity row, up to the point
    // where the two kinds differ. Returns an empty message when there is nothing
    // to send or nobody to send it to.
    StoredMessage beginAttachmentSend(const QString& fileUrl, const QString& type);
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
    // Read marks owed to this account's other devices, and the wait that batches
    // them: one send per conversation the user has been reading, not one per
    // message they scrolled past.
    QHash<QString, qint64> pendingReadSync_;
    QTimer readSyncTimer_;
    void flushReadSync();
    // Read receipts for messages this device does not hold yet, by peer. The two
    // travel as separate mailbox items and can arrive in either order.
    QHash<QString, QSet<QString>> receiptsAhead_;
    // The order the worker reported the contacts in, and what it said about each
    // of them. Two views of one refresh: the list carries the order, the map the
    // contacts themselves.
    QStringList contactFps_;
    QHash<QString, ContactState> contactState_;
    QStringList blocked_;
    QStringList recentReactions_;
    // Where this account lives and what unlocks it, for the store below.
    QString accountPath_;
    // The system note tracking a contact add, per operation id: the progress of a
    // request is written into the conversation it will belong to.
    QHash<QString, qint64> contactProgressRows_;
    // A contact request the recipient's address refused for being over its cap:
    // what it takes to send it again, and how many automatic tries are left.
    struct PendingContactRequest {
        QString uri;
        QString intro;
        int triesLeft = 0;
        // The name the first attempt gave the request. Every repeat carries it,
        // so the recipient's server recognises the second copy as the first one
        // and the recipient sees one invitation rather than one per attempt.
        QString requestId;
    };
    QHash<QString, PendingContactRequest> refusedRequests_;
    QString accountPassphrase_;
    std::unique_ptr<client::AccountDb> accountDb_;
    client::AccountDb& accountDb();
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
    bool voiceMonitoring_ = false;
    qint64 voiceElapsedMs_ = 0;
    qint64 voicePositionMs_ = 0;
    // Where the message being loaded for playback should start.
    qint64 voiceSeekMs_ = 0;
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
    // Ticks while something is playing, so the waveform fills as it goes.
    QTimer playbackTimer_;
    // The blob-retention chosen for each outgoing file (by local id), so a resend
    // reuses the same TTL / download cap. Session-only; a resend after a restart
    // falls back to the store default.
    bool connecting_ = false;
    QString connectPhase_;
    int connectPercent_ = 0;
    QString connectError_;
    bool i2pHasKey_ = false;
    bool i2pEnabled_ = false;
    bool i2pActive_ = false;
    QString i2pAddress_;
    QString i2pServedAddress_;
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
    // The storage window's figures, and whether a trim is running behind them.
    QVariantMap deviceStorage_;
    bool deviceStorageBusy_ = false;
    // Trims one conversation (or every one, when peer is empty) and rebuilds the
    // file. Shared by both invokables: they differ only in what they name.
    void runTrim(const QString& peer, int keep);
    // Marks the storage window busy, says what with, and starts the work once the
    // window has had a frame to show it: everything here holds the drawing thread
    // for as long as it runs.
    void beginStorageWork(const QString& what, const std::function<void()>& work);
    void endStorageWork();
    QString callState_ = QStringLiteral("idle");
    QString callStage_;
    qint64 callConnectedAtMs_ = 0;
    qreal callInputLevel_ = 0.0;
    qreal callOutputLevel_ = 0.0;
    QString callPeer_;
    QString callId_;
    // The call the user refused here: state updates still travelling for it are
    // dropped, so a refusal is one transition rather than a flicker.
    QString refusedCallId_;
    // Sends that went to the saved chat. Their green comes from this account's
    // own server holding the note: nobody is going to read it back.
    QSet<qint64> savedSends_;
    // This account's own signer, handed over when it opened.
    std::shared_ptr<bazarish::client::LoginSigner> loginSigner_;
    // Set once the account is being closed, so a second ask does nothing.
    bool shuttingDown_ = false;
    // The activity-panel operation id for the call currently in progress (so it is
    // finished when the call goes back to idle, even though the idle signal carries
    // no call id). Empty when there is no active call row.
    QString callOpId_;
    bool callMuted_ = false;
    // Call-progress tones, and the one thing the call state alone cannot tell:
    // whether it was this side that hung up (which is not a call that failed).
    CallTones callTones_;
    bool callEndedLocally_ = false;
    // Rebuilds the chat list from the cached contacts.
    void rebuildChatList();
    int unreadTotal_ = 0;
    // Shows a forward where it belongs and starts its activity row.
    void forwardShown(const StoredMessage& m, const QString& toPeer, const QString& preview);
    // Recomputes unreadTotal_ from the contacts model and notifies on change.
    void refreshUnreadTotal();
};

}  // namespace bazarish::app
