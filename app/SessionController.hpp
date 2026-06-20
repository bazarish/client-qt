// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "TranscriptStore.hpp"

#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantMap>

#include <memory>

class QTimer;

namespace bazarish::client {
class Session;
}

namespace bazarish::app {

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
    // Re-points the existing server connection at a new facade list (same server).
    void updateFacades(const QStringList& facadeUrls);
    void sync();
    // Starts or stops background syncing (the account going online/offline).
    void setSyncEnabled(bool on);
    void sendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId);
    void sendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId);
    void sendReceipt(const QString& peer, const QString& refId);
    void sendCallback(const QString& peer, const QString& data, const QString& ref);
    void sendCommand(const QString& peer, const QString& command, const QString& args);
    void sendEdit(const QString& peer, const QString& refId, const QString& text);
    void createGroup(const QString& name, const QStringList& memberFps);
    void sendGroupText(const QString& groupId, const QString& text, qint64 localId);
    void addGroupMembers(const QString& groupId, const QStringList& fps);
    void removeGroupMember(const QString& groupId, const QString& fp);
    void leaveGroup(const QString& groupId);
    void fetchGroupMembers(const QString& groupId);
    void addByInvite(const QString& uri, const QString& intro);
    void addByUsername(const QString& alias, const QString& intro);
    void addByFingerprint(const QString& fingerprint, const QString& intro);
    void requestInvite();
    void saveAttachment(const QString& ref, const QString& key, const QString& destPath);
    void exportProfile(const QString& path, const QString& password);
    // Per-user I2P destination: set up the master (generate or load a .dat),
    // turn the paid option on/off, and report the current status.
    void generatePersonalKey();
    void loadPersonalKey(const QString& path);
    void enablePersonalDest();
    void disablePersonalDest();
    void refreshI2pStatus();
    // Audio calls: each runs the matching Session method (strict SAM, so a
    // failure surfaces as actionFailed) and then re-emits the call state.
    void startCall(const QString& peer);
    void acceptCall(const QString& callId);
    void declineCall(const QString& callId);
    void endCall();
    void setCallMuted(bool muted);

signals:
    void opened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& subscriptionText);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& subscriptionText);
    void messageReceived(const QVariantMap& message);
    void contactsRefreshed(const QStringList& fingerprints);
    void sendProgress(qint64 localId, int state);  // 1 = accepted by own server (grey)
    void sendResult(qint64 localId, bool ok, const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);
    // Whether the last sync reached the facade (true) or failed (false).
    void syncReachable(bool ok);
    // The facade currently in use, the configured facade list, and the server
    // fingerprint, for the GUI.
    void facadeInfo(
        const QString& activeUrl, const QStringList& configured, const QString& serverFp);
    void groupsRefreshed(const QStringList& ids, const QStringList& names);
    void groupCreated(const QString& groupId, const QString& name);
    void groupMembersReady(const QString& groupId, const QStringList& members, bool iAmAdmin);
    // hasKey: a master is set up in the profile. enabled/active: the paid option
    // is on / currently paid-active. address: the personal b32 (empty if none).
    // summary: a one-line human status for the settings page.
    void i2pStatus(bool hasKey, bool enabled, bool active, const QString& address,
        const QString& summary);
    // Call lifecycle: state is 0 idle / 1 outgoing / 2 incoming / 3 active,
    // matching Session::CallState. Emitted after every sync and call action.
    void callStateChanged(int state, const QString& peer, const QString& callId, bool muted);

private:
    void ensureSyncTimer();
    void emitGroups();
    void emitFacadeInfo();
    void emitCallState();
    std::unique_ptr<bazarish::client::Session> session_;
    QTimer* syncTimer_ = nullptr;
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
    Q_PROPERTY(QString activePeer READ activePeer NOTIFY activePeerChanged)
    // The on-disk profile id this session was opened from (stable per account).
    Q_PROPERTY(QString accountId READ accountId CONSTANT)
    // Total unread across this account's conversations (for the switcher badge).
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadTotalChanged)
    Q_PROPERTY(QObject* contacts READ contacts CONSTANT)
    Q_PROPERTY(QObject* conversation READ conversation CONSTANT)
    Q_PROPERTY(bool sendReceipts READ sendReceipts WRITE setSendReceipts NOTIFY sendReceiptsChanged)
    // True while the composer is editing a previously sent message; editingText
    // is its current text, so the composer can prefill the field.
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString editingText READ editingText NOTIFY editingChanged)
    // The active group's members and whether we administer it (empty/false for a
    // one-to-one chat), for the group-info panel.
    Q_PROPERTY(QStringList activeGroupMembers READ activeGroupMembers NOTIFY activeGroupChanged)
    Q_PROPERTY(bool activeGroupAdmin READ activeGroupAdmin NOTIFY activeGroupChanged)
    // Per-user I2P destination status for the settings page.
    Q_PROPERTY(bool i2pHasKey READ i2pHasKey NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pEnabled READ i2pEnabled NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pActive READ i2pActive NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString i2pAddress READ i2pAddress NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString i2pStatusText READ i2pStatusText NOTIFY i2pStatusChanged)
    // Audio call state for the call screen: "idle"/"outgoing"/"incoming"/"active",
    // the peer fingerprint, a display name, and the local mute flag.
    Q_PROPERTY(QString callState READ callState NOTIFY callChanged)
    Q_PROPERTY(QString callPeer READ callPeer NOTIFY callChanged)
    Q_PROPERTY(QString callPeerName READ callPeerName NOTIFY callChanged)
    Q_PROPERTY(bool callMuted READ callMuted NOTIFY callChanged)
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
    QString activePeer() const { return activePeer_; }
    QString accountId() const { return profileId_; }
    int unreadTotal() const { return unreadTotal_; }
    QObject* contacts() { return &contacts_; }
    QObject* conversation() { return &conversation_; }
    bool sendReceipts() const { return sendReceipts_; }
    void setSendReceipts(bool on) { if (sendReceipts_ != on) { sendReceipts_ = on; emit sendReceiptsChanged(); } }
    bool editing() const { return editing_; }
    QString editingText() const { return editingText_; }
    QStringList activeGroupMembers() const { return activeGroupMembers_; }
    bool activeGroupAdmin() const { return activeGroupAdmin_; }
    bool i2pHasKey() const { return i2pHasKey_; }
    bool i2pEnabled() const { return i2pEnabled_; }
    bool i2pActive() const { return i2pActive_; }
    QString i2pAddress() const { return i2pAddress_; }
    QString i2pStatusText() const { return i2pStatusText_; }
    QString callState() const { return callState_; }
    QString callPeer() const { return callPeer_; }
    QString callPeerName() const { return peerName(callPeer_); }
    bool callMuted() const { return callMuted_; }

    // Opens a profile on the worker thread (dir + id + passphrase).
    void open(const QString& dir, const QString& profileId, const QString& passphrase);

    // Connects (and subscribes) through an ordered list of facade URLs
    // (http[s]://host[:port][/secret]). The client fails over across them.
    Q_INVOKABLE void connectServer(const QStringList& facadeUrls, const QString& serverFp);
    // Edits the facade list of an already-connected server.
    Q_INVOKABLE void updateFacades(const QStringList& facadeUrls);
    // Decodes a bazarish://server/... link into { serverFp, facades } for the
    // connect form to prefill; returns an empty map on a malformed link.
    Q_INVOKABLE QVariantMap parseServerLink(const QString& uri) const;
    // A shareable bazarish://server/... link for this account's server config.
    Q_INVOKABLE QString myServerLink() const;
    // Brings this account online (resume syncing) or offline (stop syncing
    // without unloading it).
    Q_INVOKABLE void goOnline();
    Q_INVOKABLE void goOffline();
    Q_INVOKABLE void openConversation(const QString& peer);
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void sendFile(const QString& fileUrl);
    // Creates a group from selected contacts and opens it.
    Q_INVOKABLE void createGroup(const QString& name, const QStringList& memberFps);
    // Whether a chat-list id is a group, and a display name for any peer/group.
    Q_INVOKABLE bool isGroup(const QString& id) const;
    Q_INVOKABLE QString peerName(const QString& id) const;
    // Group membership management (operate on the given group id).
    Q_INVOKABLE void addGroupMembers(const QString& groupId, const QStringList& fps);
    Q_INVOKABLE void removeGroupMember(const QString& groupId, const QString& fp);
    Q_INVOKABLE void leaveGroup(const QString& groupId);
    // Inline-keyboard button presses in the active conversation: a callback
    // (button data + the keyboard message's protocol id) or a command button.
    Q_INVOKABLE void sendCallback(const QString& data, const QString& refMsgId);
    Q_INVOKABLE void sendCommand(const QString& command, const QString& args);
    // Editing one's own message: start (prefilling the composer), commit the new
    // text (updates our copy and sends an edit to the peer), or cancel.
    Q_INVOKABLE void beginEdit(qint64 localId, const QString& protocolId, const QString& text);
    Q_INVOKABLE void commitEdit(const QString& newText);
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void addByInvite(const QString& uri, const QString& intro);
    Q_INVOKABLE void addByUsername(const QString& alias, const QString& intro);
    Q_INVOKABLE void addByFingerprint(const QString& fingerprint, const QString& intro);
    Q_INVOKABLE void requestInvite();
    Q_INVOKABLE void saveAttachment(const QString& ref, const QString& key, const QString& fileUrl);
    Q_INVOKABLE void exportProfile(const QString& fileUrl, const QString& password);
    Q_INVOKABLE QString shortFingerprint(const QString& fp) const;
    // Per-user I2P destination controls (drive the worker thread).
    Q_INVOKABLE void generatePersonalKey();
    Q_INVOKABLE void loadPersonalKey(const QString& fileUrl);
    Q_INVOKABLE void enablePersonalDest();
    Q_INVOKABLE void disablePersonalDest();
    Q_INVOKABLE void refreshI2pStatus();
    // Audio calls. startCall dials the active/given peer; accept/decline act on
    // the current incoming call; end hangs up; setCallMuted toggles the mic.
    Q_INVOKABLE void startCall(const QString& peer);
    Q_INVOKABLE void acceptCall();
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void endCall();
    Q_INVOKABLE void setCallMuted(bool muted);

signals:
    void identityChanged();
    void connectedChanged();
    void activePeerChanged();
    void activeGroupChanged();
    void facadeInfoChanged();
    void sendReceiptsChanged();
    void editingChanged();
    void unreadTotalChanged();
    void onlineChanged();
    void reachableChanged();
    void i2pStatusChanged();
    void callChanged();
    void openFailed(const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);

signals:  // to worker
    void requestConnect(const QStringList& facadeUrls, const QString& serverFp, int days);
    void requestUpdateFacades(const QStringList& facadeUrls);
    void requestSendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId);
    void requestSendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId);
    void requestSendReceipt(const QString& peer, const QString& refId);
    void requestSendCallback(const QString& peer, const QString& data, const QString& ref);
    void requestSendCommand(const QString& peer, const QString& command, const QString& args);
    void requestSendEdit(const QString& peer, const QString& refId, const QString& text);
    void requestCreateGroup(const QString& name, const QStringList& memberFps);
    void requestSendGroupText(const QString& groupId, const QString& text, qint64 localId);
    void requestAddGroupMembers(const QString& groupId, const QStringList& fps);
    void requestRemoveGroupMember(const QString& groupId, const QString& fp);
    void requestLeaveGroup(const QString& groupId);
    void requestFetchGroupMembers(const QString& groupId);
    void requestAddByInvite(const QString& uri, const QString& intro);
    void requestAddByUsername(const QString& alias, const QString& intro);
    void requestAddByFingerprint(const QString& fingerprint, const QString& intro);
    void requestInviteSig();
    void requestSaveAttachment(const QString& ref, const QString& key, const QString& destPath);
    void requestExport(const QString& path, const QString& password);
    void requestOpen(const QString& dir, const QString& passphrase);
    void requestSetSync(bool on);
    void requestGeneratePersonalKey();
    void requestLoadPersonalKey(const QString& path);
    void requestEnablePersonalDest();
    void requestDisablePersonalDest();
    void requestRefreshI2pStatus();
    void requestStartCall(const QString& peer);
    void requestAcceptCall(const QString& callId);
    void requestDeclineCall(const QString& callId);
    void requestEndCall();
    void requestSetCallMuted(bool muted);

private slots:
    void onOpened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& subscriptionText);
    void onConnectionChanged(bool connected, const QString& subscriptionText);
    void onMessageReceived(const QVariantMap& message);
    void onSendProgress(qint64 localId, int state);
    void onSendResult(qint64 localId, bool ok, const QString& error);
    void onSyncReachable(bool ok);
    void onFacadeInfo(
        const QString& activeUrl, const QStringList& configured, const QString& serverFp);
    void onGroupsRefreshed(const QStringList& ids, const QStringList& names);
    void onGroupCreated(const QString& groupId, const QString& name);
    void onGroupMembersReady(const QString& groupId, const QStringList& members, bool iAmAdmin);
    void onI2pStatus(bool hasKey, bool enabled, bool active, const QString& address,
        const QString& summary);
    void onCallStateChanged(int state, const QString& peer, const QString& callId, bool muted);

private:
    QThread thread_;
    SessionWorker* worker_ = nullptr;
    TranscriptStore store_;
    ContactListModel contacts_;
    ConversationModel conversation_;

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
    // Current delivery status per outgoing local id, so a later/lower signal
    // (e.g. "yellow" arriving after "green") never downgrades the tick.
    QHash<qint64, int> statusById_;
    void bumpStatus(qint64 localId, int status);
    // Groups this account belongs to (id -> name), merged into the chat list.
    QStringList contactFps_;
    QStringList groupIds_;
    QHash<QString, QString> groupNames_;
    QStringList activeGroupMembers_;
    bool activeGroupAdmin_ = false;
    bool i2pHasKey_ = false;
    bool i2pEnabled_ = false;
    bool i2pActive_ = false;
    QString i2pAddress_;
    QString i2pStatusText_;
    QString callState_ = QStringLiteral("idle");
    QString callPeer_;
    QString callId_;
    bool callMuted_ = false;
    // Rebuilds the chat list from the cached contacts + groups.
    void rebuildChatList();
    int unreadTotal_ = 0;
    // Recomputes unreadTotal_ from the contacts model and notifies on change.
    void refreshUnreadTotal();
};

}  // namespace bazarish::app
