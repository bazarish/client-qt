// Bazarish project (c) 2026
#pragma once

#include "AccountCommands.hpp"
#include "AccountEvents.hpp"
#include "AccountDb.hpp"
#include "ContactBook.hpp"
#include "Models.hpp"
#include "Picture.hpp"
#include "TranscriptStore.hpp"
#include "VoiceNote.hpp"

#include <bazarish/Limits.hpp>

#include <QColor>
#include <QHash>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace bazarish::app {

class SessionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY identityChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY identityChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool reachable READ reachable NOTIFY reachableChanged)
    Q_PROPERTY(bool i2pBusy READ i2pBusy NOTIFY i2pStatusChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool voiceRecording READ voiceRecording NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voiceElapsedMs READ voiceElapsedMs NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voicePositionMs READ voicePositionMs NOTIFY voiceChanged)
    Q_PROPERTY(qreal voiceLevel READ voiceLevel NOTIFY voiceChanged)
    Q_PROPERTY(bool voiceTakeReady READ voiceTakeReady NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voiceTakeMs READ voiceTakeMs NOTIFY voiceChanged)
    Q_PROPERTY(qint64 voiceTakeBytes READ voiceTakeBytes NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceTakeWave READ voiceTakeWave NOTIFY voiceChanged)
    Q_PROPERTY(bool voiceTakePlaying READ voiceTakePlaying NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceError READ voiceError NOTIFY voiceChanged)
    Q_PROPERTY(qreal voiceSpeed READ voiceSpeed NOTIFY voiceChanged)
    Q_PROPERTY(QString voicePlaying READ voicePlaying NOTIFY voiceChanged)
    Q_PROPERTY(bool awaitingApproval READ awaitingApproval NOTIFY approvalChanged)
    Q_PROPERTY(QString approvalNote READ approvalNote NOTIFY approvalChanged)
    Q_PROPERTY(QString activeFacadeHost READ activeFacadeHost NOTIFY facadeInfoChanged)
    Q_PROPERTY(QStringList configuredFacades READ configuredFacades NOTIFY facadeInfoChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectStateChanged)
    Q_PROPERTY(QString connectPhase READ connectPhase NOTIFY connectStateChanged)
    Q_PROPERTY(int connectPercent READ connectPercent NOTIFY connectStateChanged)
    Q_PROPERTY(QString connectError READ connectError NOTIFY connectStateChanged)
    Q_PROPERTY(QString pairUri READ pairUri NOTIFY pairingChanged)
    Q_PROPERTY(QString pairCode READ pairCode NOTIFY pairingChanged)
    Q_PROPERTY(QString pairStatus READ pairStatus NOTIFY pairingChanged)
    Q_PROPERTY(double pairProgress READ pairProgress NOTIFY pairingChanged)
    Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
    Q_PROPERTY(QString loginSendChallenge READ loginSendChallenge NOTIFY loginSendChanged)
    Q_PROPERTY(bool loginSending READ loginSending NOTIFY loginSendChanged)
    Q_PROPERTY(bool loginAccepted READ loginAccepted NOTIFY loginSendChanged)
    Q_PROPERTY(QString loginSendStatus READ loginSendStatus NOTIFY loginSendChanged)
    Q_PROPERTY(qint64 loginSendStartedAt READ loginSendStartedAt NOTIFY loginSendChanged)
    Q_PROPERTY(QString loginFetchLink READ loginFetchLink NOTIFY loginFetchChanged)
    Q_PROPERTY(bool loginFetching READ loginFetching NOTIFY loginFetchChanged)
    Q_PROPERTY(QString loginFetchStatus READ loginFetchStatus NOTIFY loginFetchChanged)
    Q_PROPERTY(QString loginFetchedChallenge READ loginFetchedChallenge NOTIFY loginFetchChanged)
    Q_PROPERTY(QString serverFingerprint READ serverFingerprint NOTIFY facadeInfoChanged)
    Q_PROPERTY(QStringList configuredReseeds READ configuredReseeds NOTIFY facadeInfoChanged)
    // The server's descriptor link, empty while the account has none.
    Q_PROPERTY(QString serverLink READ serverLink NOTIFY facadeInfoChanged)
    Q_PROPERTY(QString activePeer READ activePeer NOTIFY activePeerChanged)
    Q_PROPERTY(QString activePeerName READ activePeerName NOTIFY activePeerNameChanged)
    Q_PROPERTY(bool activeIsGroup READ activeIsGroup NOTIFY activeGroupChanged)
    Q_PROPERTY(qint64 activeChatTimer READ activeChatTimer NOTIFY activeChatTimerChanged)
    Q_PROPERTY(QVariantList activeBotMenu READ activeBotMenu NOTIFY activeBotMenuChanged)
    Q_PROPERTY(bool activeAliasLookup READ activeAliasLookup NOTIFY activePeerChanged)
    Q_PROPERTY(bool activeIsChannel READ activeIsChannel NOTIFY activeGroupChanged)
    Q_PROPERTY(qint64 activeGroupMembers READ activeGroupMembers NOTIFY activeGroupChanged)
    Q_PROPERTY(bool activeGroupWrite READ activeGroupWrite NOTIFY activeGroupChanged)
    Q_PROPERTY(bool activeGroupAdmin READ activeGroupAdmin NOTIFY activeGroupChanged)
    Q_PROPERTY(bool atNewest READ atNewest NOTIFY pagingChanged)
    Q_PROPERTY(bool hasMoreOlder READ hasMoreOlder NOTIFY pagingChanged)
    Q_PROPERTY(QString accountId READ accountId CONSTANT)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadTotalChanged)
    Q_PROPERTY(QObject* contacts READ contacts CONSTANT)
    Q_PROPERTY(QObject* chatList READ chatList CONSTANT)
    Q_PROPERTY(QObject* conversation READ conversation CONSTANT)
    Q_PROPERTY(QObject* operations READ operations CONSTANT)
    Q_PROPERTY(int activeOperations READ activeOperations NOTIFY operationsChanged)
    Q_PROPERTY(bool sendReceipts READ sendReceipts WRITE setSendReceipts NOTIFY sendReceiptsChanged)
    Q_PROPERTY(bool acceptCalls READ acceptCalls WRITE setAcceptCalls NOTIFY acceptCallsChanged)
    Q_PROPERTY(QVariantList aliasHoldings READ aliasHoldings NOTIFY aliasChanged)
    Q_PROPERTY(QString aliasNote READ aliasNote NOTIFY aliasChanged)
    Q_PROPERTY(bool aliasBusy READ aliasBusy NOTIFY aliasChanged)
    Q_PROPERTY(int delegationDays READ delegationDays WRITE setDelegationDays
            NOTIFY delegationDaysChanged)
    Q_PROPERTY(int minDelegationDays READ minDelegationDays CONSTANT)
    Q_PROPERTY(int maxDelegationDays READ maxDelegationDays CONSTANT)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString editingText READ editingText NOTIFY editingChanged)
    Q_PROPERTY(bool replying READ replying NOTIFY replyingChanged)
    Q_PROPERTY(QString replyingText READ replyingText NOTIFY replyingChanged)
    Q_PROPERTY(QString replyingSender READ replyingSender NOTIFY replyingChanged)
    Q_PROPERTY(int contactsRevision READ contactsRevision NOTIFY contactsRevisionChanged)
    Q_PROPERTY(int reactionsRevision READ reactionsRevision NOTIFY reactionsRevisionChanged)
    Q_PROPERTY(bool i2pHasKey READ i2pHasKey NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pEnabled READ i2pEnabled NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pActive READ i2pActive NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString i2pAddress READ i2pAddress NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString i2pServedAddress READ i2pServedAddress NOTIFY i2pStatusChanged)
    Q_PROPERTY(bool i2pAddressMismatch READ i2pAddressMismatch NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString i2pServerState READ i2pServerState NOTIFY i2pStatusChanged)
    Q_PROPERTY(QString acceptingContact READ acceptingContact NOTIFY acceptingContactChanged)
    Q_PROPERTY(qint64 i2pTransientExpires READ i2pTransientExpires NOTIFY i2pStatusChanged)
    Q_PROPERTY(QVariantMap storageInfo READ storageInfo NOTIFY storageChanged)
    Q_PROPERTY(QVariantMap deviceStorage READ deviceStorage NOTIFY deviceStorageChanged)
    Q_PROPERTY(int keepRecentMessages READ keepRecentMessages CONSTANT)
    Q_PROPERTY(int keepManyMessages READ keepManyMessages CONSTANT)
    Q_PROPERTY(QString callState READ callState NOTIFY callChanged)
    Q_PROPERTY(QString callStage READ callStage NOTIFY callChanged)
    Q_PROPERTY(qint64 callConnectedAtMs READ callConnectedAtMs NOTIFY callChanged)
    Q_PROPERTY(qreal callInputLevel READ callInputLevel NOTIFY callLevelsChanged)
    Q_PROPERTY(qreal callOutputLevel READ callOutputLevel NOTIFY callLevelsChanged)
    Q_PROPERTY(QString callPeer READ callPeer NOTIFY callChanged)
    Q_PROPERTY(QString callPeerName READ callPeerName NOTIFY callChanged)
    Q_PROPERTY(bool callMuted READ callMuted NOTIFY callChanged)
public:
    explicit SessionController(const QString& accountId, QObject* parent = nullptr);
    ~SessionController() override;

    AccountCommands& commands() { return commands_; }
    AccountEvents& events() { return events_; }

    QString fingerprint() const { return fingerprint_; }
    QString displayName() const { return displayName_; }
    bool connected() const { return connected_; }
    bool online() const { return online_; }
    bool reachable() const { return reachable_; }
    bool i2pBusy() const { return i2pBusy_; }
    QVariantList devices() const { return devices_; }
    bool voiceRecording() const { return voiceRecording_; }
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
    QString serverLink() const;
    QStringList configuredReseeds() const { return configuredReseeds_; }
    QString activePeer() const { return activePeer_; }
    QString activePeerName() const { return peerName(activePeer_); }
    bool atNewest() const;
    bool hasMoreOlder() const;
    QString accountId() const { return accountId_; }
    bool historyIsOpen() const { return store_.isOpen(); }
    int unreadTotal() const { return unreadTotal_; }
    QObject* contacts() { return &contacts_; }
    QObject* chatList() { return &contactsProxy_; }
    Q_INVOKABLE void setChatFilter(const QString& text);
    Q_INVOKABLE void pinChat(const QString& peer, bool pinned);
    QObject* conversation() { return &conversation_; }
    QObject* operations() { return &operations_; }
    int activeOperations() const { return operations_.runningCount(); }
    bool acceptCalls() const { return acceptCalls_; }
    QVariantList aliasHoldings() const { return aliasHoldings_; }
    QString aliasNote() const { return aliasNote_; }
    bool aliasBusy() const { return aliasBusy_; }
    Q_INVOKABLE void activateAliasServicing();
    int delegationDays() const { return delegationDays_; }
    static int minDelegationDays() { return static_cast<int>(bazarish::kMinDelegationDays); }
    static int maxDelegationDays() { return static_cast<int>(bazarish::kMaxDelegationDays); }
    void setDelegationDays(int days);
    void setAcceptCalls(bool on);
    bool sendReceipts() const { return sendReceipts_; }
    void setSendReceipts(bool on);
    bool editing() const { return editing_; }
    QString editingText() const { return editingText_; }
    bool replying() const { return replying_; }
    QString replyingText() const { return replyingText_; }
    QString replyingSender() const { return replyingSender_; }
    int contactsRevision() const { return contactsRevision_; }
    int reactionsRevision() const { return reactionsRevision_; }
    bool activeIsGroup() const { return activeGroup_.has_value(); }
    qint64 activeChatTimer() const { return activeChatTimer_; }
    QVariantList activeBotMenu() const { return activeBotMenu_; }
    bool activeAliasLookup() const;
    bool activeIsChannel() const
    {
        return activeGroup_ && activeGroup_->kind == QStringLiteral("channel");
    }
    qint64 activeGroupMembers() const { return activeGroup_ ? activeGroup_->members : 0; }
    bool activeGroupWrite() const { return !activeGroup_ || activeGroup_->write; }
    bool activeGroupAdmin() const { return activeGroup_ && activeGroup_->admin; }
    Q_INVOKABLE QString letterOf(const QString& name) const;
    Q_INVOKABLE QColor letterFill(const QString& name) const;
    Q_INVOKABLE QVariantList invitesIn(const QString& text) const;
    Q_INVOKABLE QString withoutInvites(const QString& text) const;
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
    QString i2pServerState() const { return i2pServerState_; }
    QString acceptingContact() const { return acceptingContact_; }
    QVariantMap storageInfo() const;
    QVariantMap deviceStorage() const { return deviceStorage_; }
    int keepRecentMessages() const { return kKeepRecentMessages; }
    int keepManyMessages() const { return kKeepManyMessages; }
    qint64 i2pTransientExpires() const { return i2pTransientExpires_; }
    QString callState() const { return callState_; }
    QString callStage() const;
    qint64 callConnectedAtMs() const { return callConnectedAtMs_; }
    qreal callInputLevel() const { return callInputLevel_; }
    qreal callOutputLevel() const { return callOutputLevel_; }
    QString callPeer() const { return callPeer_; }
    QString callPeerName() const { return peerName(callPeer_); }
    bool callMuted() const { return callMuted_; }


    Q_INVOKABLE void connectServer(const QStringList& facadeUrls, const QString& serverFp,
        const QStringList& reseedUrls = {});
    Q_INVOKABLE QVariantMap parseServerLink(const QString& uri) const;
    void retranslate();
    Q_INVOKABLE void openConversation(const QString& peer);
    Q_INVOKABLE void closeConversation();
    Q_INVOKABLE void openConversationAtMessage(const QString& peer, qint64 localId);
    Q_INVOKABLE int loadOlderMessages();
    Q_INVOKABLE int loadNewerMessages();
    Q_INVOKABLE void jumpToLatest();
    Q_INVOKABLE void saveScroll(const QString& peer, int anchorRow, bool stick);
    Q_INVOKABLE QVariantMap scrollFor(const QString& peer) const;
    Q_INVOKABLE void markReadThroughRow(int row);
    Q_INVOKABLE QVariantList searchMessages(const QString& query);
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void sendOffered(const QString& text);
    Q_INVOKABLE void forwardMessage(const QString& e2eId, const QString& toPeer);
    // Several messages of the open chat, by local id; each is taken in the order the chat shows.
    Q_INVOKABLE int copyMessages(const QVariantList& localIds) const;
    Q_INVOKABLE void forwardMessages(const QVariantList& localIds, const QString& toPeer);
    Q_INVOKABLE void deleteMessages(const QVariantList& localIds);
    Q_INVOKABLE void resendText(qint64 localId, const QString& text, const QString& e2eId);
    Q_INVOKABLE void sendFile(const QString& fileUrl);
    Q_INVOKABLE void sendPictureFiles(const QStringList& fileUrls);
    Q_INVOKABLE QVariantMap pictureBeside(const QString& e2eId, int step);
    // The address a bubble draws its picture from; bytes the cache let go come back first.
    Q_INVOKABLE QString pictureSource(const QString& e2eId, int revision);
    Q_INVOKABLE void savePictureToDownloads(const QString& e2eId, const QString& name);
    Q_INVOKABLE void sendClipboardPicture();
    Q_INVOKABLE void sendShot(QObject* shot);
    Q_INVOKABLE void savePictureAs(const QString& e2eId, const QString& fileUrl);
    Q_INVOKABLE void copyPicture(const QString& e2eId);

    Q_INVOKABLE void startVoiceRecording();
    Q_INVOKABLE void stopVoiceRecording();
    Q_INVOKABLE void cancelVoiceRecording();
    Q_INVOKABLE void playVoiceTake();
    void stopVoiceTake();
    Q_INVOKABLE void sendVoiceTake();
    Q_INVOKABLE void discardVoiceTake();
    Q_INVOKABLE void playVoice(const QString& e2eId, qint64 fromMs = -1);
    void stopVoice();
    Q_INVOKABLE void cycleVoiceSpeed();
    Q_INVOKABLE QUrl defaultPictureSaveUrl(const QString& e2eId, const QString& name) const;
    Q_INVOKABLE void resendFile(qint64 localId, const QString& e2eId);
    Q_INVOKABLE void resendVoice(qint64 localId, const QString& e2eId);
    Q_INVOKABLE void resendPicture(qint64 localId, const QString& e2eId);
    Q_INVOKABLE QString peerName(const QString& id) const;
    QString savedPeer() const;
    static QString savedChatName();
    static QString attachmentLabel(const QString& type);
    QString chatPreview(const QString& peer) const;
    Q_INVOKABLE bool isSavedChat(const QString& peer) const
    {
        return !peer.isEmpty() && peer == savedPeer();
    }
    Q_INVOKABLE void clearSavedEverywhere();

    Q_INVOKABLE bool isBlocked(const QString& peer) const;
    Q_INVOKABLE void setBlocked(const QString& peer, bool blocked);
    Q_INVOKABLE QVariantList blockedList() const;
    Q_INVOKABLE bool contactNotifications(const QString& peer) const;
    Q_INVOKABLE bool contactCalls(const QString& peer) const;
    Q_INVOKABLE void setContactNotifications(const QString& peer, bool on);
    Q_INVOKABLE void setContactCalls(const QString& peer, bool allowed);
    Q_INVOKABLE QString contactName(const QString& fp) const;
    Q_INVOKABLE void setAvatarFromGrab(QObject* grab);
    Q_PROPERTY(bool hasAvatar READ hasAvatar NOTIFY avatarChanged)
    bool hasAvatar() const;
    Q_PROPERTY(bool avatarBusy READ avatarBusy NOTIFY avatarChanged)
    bool avatarBusy() const { return avatarBusy_; }
    Q_INVOKABLE void clearAvatar();
    Q_INVOKABLE void setDisplayName(const QString& name);
    Q_INVOKABLE void react(const QString& e2eId, const QString& emoji);
    Q_PROPERTY(QStringList standardReactions READ standardReactions CONSTANT)
    QStringList standardReactions() const;
    Q_PROPERTY(QStringList recentReactions READ recentReactions NOTIFY recentReactionsChanged)
    QStringList recentReactions() const { return recentReactions_; }
    Q_PROPERTY(QStringList recentEmoji READ recentEmoji NOTIFY recentEmojiChanged)
    QStringList recentEmoji() const { return recentEmoji_; }
    Q_INVOKABLE void rememberEmoji(const QString& emoji);
    Q_INVOKABLE QVariantList reactionSummary(const QString& e2eId) const;
    Q_INVOKABLE void renameContact(const QString& fp, const QString& name);
    Q_INVOKABLE QString contactInvite(const QString& fp) const;
    Q_INVOKABLE bool canWriteTo(const QString& fp) const;
    Q_PROPERTY(QString ownInvite READ ownInvite NOTIFY ownInviteChanged)
    QString ownInvite() const { return ownInvite_; }
    Q_INVOKABLE void clearChat(bool forEveryone);
    // The auto-delete timers a chat can carry, in seconds, and how each one is named.
    Q_INVOKABLE QVariantList chatTimerChoices() const;
    Q_INVOKABLE QString chatTimerLabel(qint64 seconds) const;
    Q_INVOKABLE void setChatTimer(qint64 seconds);
    Q_INVOKABLE void deleteContact();
    Q_INVOKABLE void retryContactAdd();
    Q_INVOKABLE void sendCallback(
        const QString& data, const QString& refMsgId, const QString& label = {});
    Q_INVOKABLE void sendCommand(
        const QString& command, const QString& args, const QString& label = {});
    Q_INVOKABLE void beginEdit(qint64 localId, const QString& e2eId, const QString& text);
    Q_INVOKABLE void commitEdit(const QString& newText);
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void beginReply(
        const QString& e2eId, const QString& previewText, const QString& sender);
    Q_INVOKABLE void cancelReply();
    Q_INVOKABLE QVariantMap replyPreview(const QString& e2eId) const;
    Q_INVOKABLE void deleteMessage(qint64 localId, const QString& e2eId, bool outgoing);
    Q_INVOKABLE QString inviteProblem(const QString& uri) const;
    Q_INVOKABLE QString aliasProblem(const QString& typed) const;
    Q_INVOKABLE void addByInvite(const QString& uri, const QString& intro);
    Q_INVOKABLE void retryContactRequest(const QString& fingerprint);
    Q_INVOKABLE void addByAlias(const QString& alias, const QString& intro);
    Q_INVOKABLE void acceptContact();
    Q_INVOKABLE bool contactCanAccept(const QString& fp) const;
    Q_INVOKABLE bool contactAgreeing(const QString& fp) const;
    Q_INVOKABLE QStringList reactionsToFlash(const QString& e2eId) const;
    Q_INVOKABLE void forgetReactionFlash();
    Q_INVOKABLE void requestInvite();
    Q_INVOKABLE void signLogin(const QString& challenge);
    Q_INVOKABLE void sendLogin(const QString& challenge);
    Q_INVOKABLE void fetchLogin(const QString& link);
    Q_INVOKABLE void cancelLoginFetch(const QString& link);
    Q_INVOKABLE QVariantMap describeLoginChallenge(const QString& challenge) const;
    Q_INVOKABLE void askForContacts();
    Q_INVOKABLE void refreshConnectionLog();
    Q_INVOKABLE void clearConnectionLog();
    Q_INVOKABLE void saveAttachmentToFile(const QString& peer, const QString& e2eId,
        const QString& fileUrl, qint64 token);
    Q_INVOKABLE QUrl defaultSaveUrl(const QString& fileName) const;
    Q_INVOKABLE bool fileExists(const QString& path) const;
    Q_INVOKABLE void showInFolder(const QString& path) const;
    Q_INVOKABLE void exportAccount(const QString& fileUrl, const QString& password);
    Q_INVOKABLE void startPairing();
    Q_INVOKABLE void stopPairing();
    QString pairUri() const { return pairUri_; }
    QString pairCode() const { return pairCode_; }
    QString pairStatus() const { return pairStatus_; }
    QString loginSendChallenge() const { return loginSendChallenge_; }
    bool loginSending() const { return loginSendPhase_ == eLoginSending; }
    bool loginAccepted() const { return loginSendPhase_ == eLoginAccepted; }
    QString loginSendStatus() const { return loginSendStatus_; }
    qint64 loginSendStartedAt() const { return loginSendStartedAt_; }
    QString loginFetchLink() const { return loginFetchLink_; }
    bool loginFetching() const { return loginFetching_; }
    QString loginFetchStatus() const { return loginFetchStatus_; }
    QString loginFetchedChallenge() const { return loginFetchedChallenge_; }
    double pairProgress() const { return pairProgress_; }
    bool pairing() const { return pairing_; }
    Q_INVOKABLE void changePassphrase(const QString& passphrase);
    Q_INVOKABLE QString shortFingerprint(const QString& fp) const;
    Q_INVOKABLE void generatePersonalKey();
    Q_INVOKABLE void loadPersonalKey(const QString& fileUrl);
    Q_INVOKABLE void replacePersonalKey();
    Q_INVOKABLE void setAliasBinding(const QString& alias, bool on);
    Q_INVOKABLE void retryRoutingTo(const QString& peer);
    Q_INVOKABLE void cancelTransfer(const QString& e2eId);
    Q_INVOKABLE void publishPersonalDest();
    Q_INVOKABLE void disablePersonalDest();
    Q_INVOKABLE void refreshI2pStatus();
    Q_INVOKABLE void keepThisDeviceAddress();
    Q_INVOKABLE void useFreshAddress();
    Q_INVOKABLE void refreshStorageUsage();
    Q_INVOKABLE void measureDeviceStorage();
    Q_INVOKABLE void trimChat(const QString& peer, int keep);
    Q_INVOKABLE void trimEveryChat(int keep);
    Q_INVOKABLE void compactDatabase();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void forgetDevice(const QString& clientId);
    Q_INVOKABLE void startCall(const QString& peer);
    Q_INVOKABLE void acceptCall();
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void endCall();
    Q_INVOKABLE void setCallMuted(bool muted);

signals:
    void recentReactionsChanged();
    void recentEmojiChanged();
    void avatarChanged();
    void identityChanged();
    void connectedChanged();
    void activePeerChanged();
    void activePeerNameChanged();
    void activeGroupChanged();
    void activeChatTimerChanged();
    void activeBotMenuChanged();
    void pagingChanged();
    void scrollToMessage(qint64 localId);
    void scrollToUnread(qint64 firstUnreadId);
    void scrollToBottom();
    void facadeInfoChanged();
    void connectStateChanged();
    void sendReceiptsChanged();
    void acceptCallsChanged();
    void delegationDaysChanged();
    void contactRetryExhausted(const QString& fingerprint);
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
    void i2pAnswered();
    void devicesChanged();
    void voiceChanged();
    void storageChanged();
    void deviceStorageChanged();
    void callChanged();
    void callLevelsChanged();
    void openFailed(const QString& error);
    void historyOpened();
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);
    void inviteUnavailable(const QString& reason);
    void loginSigned(const QString& blob);
    void addressNeedsChoice(const QString& servedHost, const QString& ourHost);
    void connectionLogUpdated(const QVariantList& lines);
    void resendFilePickRequested();
    void serverHello(const QString& reason, const QString& message, const QStringList& links);

signals:
    void pairingChanged();
    void loginSendChanged();
    void loginFetchChanged();
    void pairingFinished(bool ok);
    void aliasChanged();

private slots:
    void onOpened(const QString& fingerprint, const QString& displayName, bool connected);
    void onConnectionChanged(bool connected, const QString& connectionNote);
    void onAvatarReady(const QString& fingerprint, const QByteArray& data);
    void dropChat(const QString& peer);
    void onContactAccepted(const QString& peer, bool ok, const QString& reason);
    void onUploadProgress(qint64 localId, qint64 sent, qint64 total);
    void onDownloadProgress(qint64 token, qint64 received, qint64 total);
    void onServedProgress(const QString& peer, const QString& e2eId, qint64 sent,
        qint64 total);
    void onTransferStage(const QString& peer, const QString& e2eId, const QString& stage);
    void onServedFinished(const QString& peer, const QString& e2eId, bool ok,
        const QString& error);
    void onDownloadFinished(qint64 token, bool ok, const QString& error);
    void onPairOfferReady(const QString& uri, const QString& code);
    void onPairStage(const QString& status, double progress);
    void onPairFinished(bool ok, const QString& status);
    void onApprovalState(bool pending, const QString& note);
    void onConnectProgress(int percent, const QString& phase);
    void onFacadeInfo(const QString& activeUrl, const QStringList& configured,
        const QString& serverFp, const QStringList& reseeds);
    void onI2pStatus(bool hasKey, bool enabled, bool active, const QString& address,
        qint64 transientExpires, const QString& serverState);
    void onI2pKeyState(bool hasKey, const QString& address);
    void onDevicesReady(const QVariantList& devices);
    void onVoiceLoaded(const QString& e2eId, const QByteArray& bytes);
    void requestPicturesFor(const QList<StoredMessage>& messages);
    QVector<StoredMessage> chosenMessages(const QVariantList& localIds) const;
    void onStorageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota);
    void onCallStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, bool peerRinging, qint64 connectedAtMs, float inputLevel,
        float outputLevel);
    bool speakerPresent();

private:
    AccountCommands commands_;
    AccountEvents events_;
    TranscriptReader store_;
    ContactListModel contacts_;
    QSortFilterProxyModel contactsProxy_;
    ConversationModel conversation_;
    OperationListModel operations_;

    void deliverText(const QString& text, const QString& replyTo);
    void unblockBeforeWriting(const QString& peer);

    QString accountId_;
    QString fingerprint_;
    QString displayName_;
    bool connected_ = false;
    bool online_ = false;
    bool reachable_ = false;
    bool i2pBusy_ = false;
    bool flashOnNextStatus_ = false;
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
    std::optional<GroupState> activeGroup_;
    qint64 activeChatTimer_ = 0;
    QVariantList activeBotMenu_;
    QVariantList aliasHoldings_;
    QString aliasNote_;
    bool aliasBusy_ = false;
    int delegationDays_ = static_cast<int>(bazarish::kDefaultDelegationDays);
    bool editing_ = false;
    qint64 editingLocalId_ = 0;
    QString editingE2eId_;
    QString editingText_;
    bool replying_ = false;
    QString replyingE2eId_;
    QString replyingText_;
    QString replyingSender_;
    QStringList reactionsToFlash_;
    int contactsRevision_ = 0;
    int reactionsRevision_ = 0;
    QString takeReply();
    void sendPictureTo(const QString& peer, const PreparedPicture& picture, const QString& replyTo);
    void sendPreparedPicture(const PreparedPicture& picture);
    void setAvatarBusy(bool busy);
    qint64 oldestLoadedId_ = 0;
    qint64 newestLoadedId_ = 0;
    bool hasMoreOlder_ = false;
    bool hasMoreNewer_ = false;
    QString scrollPeer_;
    int scrollAnchorRow_ = -1;
    bool scrollStick_ = true;
    void activateConversation(const QString& peer);
    void refreshActiveGroup();
    void refreshChatRow(const QString& peer);
    void onRowAdded(const QString& peer, qint64 id, bool own);
    void onRowChanged(const QString& peer, qint64 id);
    void onRowRemoved(const QString& peer, qint64 id);
    void onPeerReset(const QString& peer);
    void onPeerForgotten(const QString& peer);
    void onDeliveredThrough(const QString& peer, qint64 uptoId);
    void loadLatestWindow();
    void openWindowAtUnread(const QString& peer, qint64 firstUnread);
    void showInActiveView(const StoredMessage& m, bool isOwn);
    ContactBook book_;
    QStringList recentReactions_;
    QStringList recentEmoji_;
    QString ownInvite_;
    QHash<QString, qint64> pictureOwners_;
    VoiceNote* voiceNote();
    std::unique_ptr<VoiceNote> voice_;
    QTimer voiceTimer_;
    bool voiceRecording_ = false;
    qint64 voiceElapsedMs_ = 0;
    qint64 voicePositionMs_ = 0;
    qint64 voiceSeekMs_ = 0;
    qreal voiceLevel_ = 0.0;
    QByteArray voiceTake_;
    qint64 voiceTakeMs_ = 0;
    QString voiceTakeWave_;
    bool voiceTakePlaying_ = false;
    QString voiceError_;
    int voiceSpeedStep_ = 0;
    QString voicePlaying_;
    QTimer playbackTimer_;
    bool connecting_ = false;
    QString connectPhase_;
    int connectPercent_ = 0;
    QString connectError_;
    QString pairUri_;
    QString pairCode_;
    QString pairStatus_;
    QString loginSendChallenge_;
    int loginSendPhase_ = eLoginRefused;
    QString loginSendStatus_;
    qint64 loginSendStartedAt_ = 0;
    QString loginFetchLink_;
    bool loginFetching_ = false;
    QString loginFetchStatus_;
    QString loginFetchedChallenge_;
    double pairProgress_ = kProgressUnknown;
    bool pairing_ = false;
    bool i2pHasKey_ = false;
    bool i2pEnabled_ = false;
    bool i2pActive_ = false;
    QString i2pAddress_;
    QString i2pServedAddress_;
    QVariantList devices_;
    QString i2pServerState_;
    QString acceptingContact_;
    struct TransferProgress {
        QString peer;
        QString stage;
        qint64 sent = 0;
        qint64 total = 0;
        bool finished = false;
        bool ok = false;
        QString error;
    };
    QHash<QString, TransferProgress> transfers_;
    void replayTransfersForActivePeer();
    qint64 i2pTransientExpires_ = 0;
    bool storageMailboxOk_ = false;
    quint64 storageMailboxUsed_ = 0;
    quint64 storageMailboxQuota_ = 0;
    qint64 storageUpdatedAtMs_ = 0;
    QVariantMap deviceStorage_;
    bool deviceStorageBusy_ = false;
    void runTrim(const QString& peer, int keep);
    void beginStorageWork(const QString& what, const std::function<void()>& work);
    void endStorageWork();
    QString callState_ = QStringLiteral("idle");
    QString callStage_;
    qint64 callConnectedAtMs_ = 0;
    qreal callInputLevel_ = 0.0;
    qreal callOutputLevel_ = 0.0;
    QString callPeer_;
    QString callId_;
    bool callMuted_ = false;
    void rebuildChatList();
    int unreadTotal_ = 0;
    void refreshUnreadTotal();
};

}  // namespace bazarish::app
