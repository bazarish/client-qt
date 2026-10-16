// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QMetaMethod>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace bazarish::app {

// These open an activity row of their own; every other command is shown only while it waits.
inline const QSet<QByteArray> kSelfDescribingCommands = {
    "requestOpen", "requestConnect", "requestShutdown", "requestSendText", "requestSendFile",
    "requestSendPicture", "requestSendVoice", "requestSendCallback", "requestSendCommand",
    "requestAddByInvite", "requestAddByAlias", "requestAcceptContact", "requestInviteSig",
    "requestExport", "requestSaveAttachment", "requestActivateAliasServicing",
    "requestPublishThisDeviceAddress", "requestPublishFreshAddress", "requestRefreshI2pStatus",
    "requestRefreshStorageUsage", "requestRefreshDevices", "requestForgetDevice",
    "requestCloseAccountOnServer", "requestGeneratePersonalKey", "requestLoadPersonalKey",
    "requestReplacePersonalKey", "requestPublishPersonalDest", "requestSetAliasBinding",
};

// A request goes to the worker; the core answers every other command itself.
inline bool listedWhileQueued(const QMetaMethod& command)
{
    return command.methodType() == QMetaMethod::Signal && command.name().startsWith("request")
        && !kSelfDescribingCommands.contains(command.name());
}

class AccountCommands : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void viewConversation(const QString& peer);
    void markRead(const QString& peer, qint64 id, const QString& e2eId, qint64 sentAtMs);
    void forgetReactionFlash(const QString& peer);
    void sendText(
        const QString& peer, const QString& text, const QString& replyTo, const QString& e2eId);
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
    void pinChat(const QString& peer, bool pinned);
    void clearChat(const QString& peer, bool forEveryone);
    void forgetChat(const QString& peer);
    void clearSavedEverywhere();
    void addByInvite(const QString& uri, const QString& intro);
    void addByAlias(const QString& alias, const QString& intro);
    void retryContactAdd(const QString& peer);
    void retryContactRequest(const QString& fingerprint);
    void declineCall();
    void endCall();
    void opBegin(const QString& id, const QString& kind, const QString& title,
        const QString& status, const QString& peer, const QString& cancelId);
    void opUpdate(const QString& id, const QString& status, const QString& detail,
        double progress);
    void opSetCancelId(const QString& id, const QString& cancelId);
    void opFinish(const QString& id, bool ok, const QString& status);
    void requestPublishThisDeviceAddress();
    void requestPublishFreshAddress();
    void requestConnect(const QStringList& facadeUrls, const QString& serverFp,
        const QStringList& reseedUrls);
    void requestSendText(const QString& peer, const QString& text, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void requestSendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void requestSendPicture(const QString& peer, const QByteArray& bytes, const QString& name,
        const QString& mime, qint64 localId, const QString& e2eId, const QString& replyTo,
        bool forwarded = false);
    void requestSendVoice(const QString& peer, const QByteArray& opus, qint64 durationMs,
        qint64 localId, const QString& e2eId, const QString& replyTo, bool forwarded = false);
    void requestSendReceipt(const QString& peer, const QString& refId);
    void requestAckPending(const QString& pendingId);
    void requestSendReaction(const QString& peer, const QString& refId, const QString& emoji);
    void requestSendCallback(const QString& opId, const QString& peer, const QString& data,
        const QString& ref);
    void requestSendCommand(const QString& opId, const QString& peer, const QString& command,
        const QString& args);
    void requestSendEdit(const QString& peer, const QString& refId, qint64 localId,
        const QString& text);
    void requestSendDelete(const QString& peer, const QString& refId);
    void requestUnsend(const QString& refId);
    void requestSetAvatar(const QByteArray& jpeg);
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
    void requestAddByAlias(const QString& alias, const QString& intro, const QString& opId,
        const QString& requestId);
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
    void requestStartPairing();
    void requestStopPairing();
    void requestChangePassphrase(const QString& passphrase);
    void requestActivateAliasServicing();
    void requestOpen(const QString& dir, const QString& passphrase, bool startOnline);
    void requestSetSync(bool on);
    void requestRebuildI2p();
    void requestCancelTransfer(const QString& e2eId);
    void requestGeneratePersonalKey();
    void requestLoadPersonalKey(const QString& path);
    void requestReplacePersonalKey();
    void requestSetAliasBinding(const QString& alias, bool on);
    void requestRetryRoutingTo(const QString& peer);
    void requestSetAcceptCalls(bool accept);
    void requestSetSendReceipts(bool on);
    void requestSetDelegationDays(int days);
    void requestPublishPersonalDest();
    void requestDisablePersonalDest();
    void requestRefreshI2pStatus();
    void requestRefreshStorageUsage();
    void requestRefreshDevices();
    void requestForgetDevice(const QString& clientId);
    void requestForgetPendingAdd(const QString& opId);
    void requestCloseAccountOnServer();
    void requestStartCall(const QString& peer);
    void requestAcceptCall(const QString& callId);
    void requestDeclineCall(const QString& callId);
    void requestEndCall();
    void requestSetCallMuted(bool muted);
};

}  // namespace bazarish::app
