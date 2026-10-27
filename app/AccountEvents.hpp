// Bazarish project (c) 2026
#pragma once

#include "Wire.hpp"

#include <QByteArray>
#include <QDataStream>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

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
    QString kind;
    Request request = eAnswered;

    friend bool operator==(const ContactState&, const ContactState&) = default;
};

struct OperationRow {
    QString id;
    QString kind;
    QString title;
    QString status;
    QString detail;
    double progress = kProgressUnknown;
    int state = 0;
    qint64 startedAt = 0;
    QString peer;
    QString cancelId;
};

inline QDataStream& operator<<(QDataStream& out, const ContactState& contact)
{
    return out << contact.fingerprint << contact.name << contact.invite << contact.writable
               << contact.notifications << contact.calls << contact.kind
               << static_cast<qint32>(contact.request);
}

inline QDataStream& operator>>(QDataStream& in, ContactState& contact)
{
    qint32 request = 0;
    in >> contact.fingerprint >> contact.name >> contact.invite >> contact.writable
        >> contact.notifications >> contact.calls >> contact.kind >> request;
    contact.request = static_cast<ContactState::Request>(request);
    return in;
}

inline QDataStream& operator<<(QDataStream& out, const OperationRow& row)
{
    return out << row.id << row.kind << row.title << row.status << row.detail << row.progress
               << static_cast<qint32>(row.state) << row.startedAt << row.peer << row.cancelId;
}

inline QDataStream& operator>>(QDataStream& in, OperationRow& row)
{
    qint32 state = 0;
    in >> row.id >> row.kind >> row.title >> row.status >> row.detail >> row.progress >> state
        >> row.startedAt >> row.peer >> row.cancelId;
    row.state = state;
    return in;
}

enum OperationState { eOpRunning = 0, eOpDone = 1, eOpFailed = 2 };
enum LoginSendPhase { eLoginSending = 0, eLoginAccepted = 1, eLoginRefused = 2 };

inline constexpr Latch kAccountLatches[] = {
    {"transcriptOpened"},
    {"opened"},
    {"renamed"},
    {"accountRenamed"},
    {"presence"},
    {"connectionChanged"},
    {"connectProgress"},
    {"facadeInfo"},
    {"contactsRefreshed"},
    {"accountSettings"},
    {"approvalState"},
    {"i2pStatus"},
    {"i2pKeyState"},
    {"i2pServedAddress"},
    {"callStateChanged"},
    {"reactionsToFlash"},
    {"recentReactions"},
    {"recentEmoji"},
    {"aliasHoldings"},
    {"avatarReady", 1},
    {"peerPresence", 1},
    {"loginSendState"},
};

class AccountEvents : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void addressMismatch(const QString& servedHost, const QString& ourHost);
    void pairOfferReady(const QString& uri, const QString& code);
    void pairStage(const QString& status, double progress);
    void pairFinished(bool ok, const QString& status);
    void accountClosed(bool ok, const QString& error);
    void accountSettings(bool acceptCalls, bool sendReceipts, bool showPresence);
    void peerPresence(const QString& peer, bool online, bool typing);
    void aliasHoldings(const QVariantList& rows, const QString& note);
    void aliasActivationDone(bool ok, const QString& text);
    void opened(const QString& fingerprint, const QString& displayName, bool connected);
    void renamed(const QString& newName);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& connectionNote);
    void connectProgress(int percent, const QString& phase);
    void contactsRefreshed(const QVector<ContactState>& contacts, const QStringList& blocked);
    void avatarReady(const QString& fingerprint, const QByteArray& data);
    void uploadProgress(qint64 localId, qint64 sent, qint64 total);
    void downloadProgress(qint64 token, qint64 received, qint64 total);
    void servedProgress(const QString& peer, const QString& e2eId, qint64 sent, qint64 total);
    void transferStage(const QString& peer, const QString& e2eId, const QString& stage);
    void servedFinished(const QString& peer, const QString& e2eId, bool ok, const QString& error);
    void downloadFinished(qint64 token, bool ok, const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void contactAccepted(const QString& peer, bool ok, const QString& reason);
    void inviteReady(const QString& uri);
    void inviteUnavailable(const QString& reason);
    void loginSigned(const QString& blob);
    void loginSendState(
        const QString& challenge, int phase, const QString& status, qint64 startedAtMs);
    void loginFetchState(
        const QString& link, bool fetching, const QString& status, const QString& challenge);
    void connectionLogReady(const QVariantList& lines);
    void approvalState(bool pending, const QString& note);
    void facadeInfo(const QString& activeAddress, const QStringList& configured,
        const QString& serverFp, const QStringList& reseeds);
    void i2pStatus(bool hasKey, bool delegated, bool live, const QString& address,
        qint64 transientExpires, const QString& serverState);
    void i2pKeyState(bool hasKey, const QString& address);
    void i2pServedAddress(const QString& address);
    void devicesReady(const QVariantList& devices);
    void storageUsageReady(bool mailboxOk, qulonglong mailboxUsed, qulonglong mailboxQuota);
    void serverHello(const QString& reason, const QString& message, const QStringList& links);
    void callStateChanged(int state, const QString& peer, const QString& callId, bool muted,
        const QString& stage, bool peerRinging, qint64 connectedAtMs, float inputLevel,
        float outputLevel);
    void closed();
    void rowAdded(const QString& peer, qint64 id, bool own);
    void rowChanged(const QString& peer, qint64 id);
    void rowRemoved(const QString& peer, qint64 id);
    void peerReset(const QString& peer);
    void chatTimerChanged(const QString& peer, qint64 seconds);
    void botMenuChanged(const QString& peer);
    void peerForgotten(const QString& peer);
    void chatChanged(const QString& peer);
    void chatsChanged();
    void deliveredThrough(const QString& peer, qint64 uptoId);
    void reactionsChanged();
    void reactionsToFlash(const QStringList& flashing);
    void recentReactions(const QStringList& recent);
    void recentEmoji(const QStringList& recent);
    void groupChanged(const QString& peer);
    void accountRenamed(const QString& name);
    void contactsRevised();
    void opChanged(const OperationRow& row);
    void opRemoved(const QString& id);
    void messageError(qint64 localId, const QString& error);
    void resendFilePickRequested();
    void showConversation(const QString& peer);
    void contactRetryExhausted(const QString& fingerprint);
    void contactNamed(const QString& peer, const QString& name);
    void storageWorkDone();
    void transcriptOpened(const QString& path, const QByteArray& key);
    void presence(bool online, bool reachable, const QString& gatewayRefusal);
};

}  // namespace bazarish::app
