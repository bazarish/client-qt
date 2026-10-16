// Bazarish project (c) 2026
#pragma once

#include "AccountCommands.hpp"
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
    CoreAccount(AccountCommands& commands, AccountEvents& events, QObject* parent = nullptr);
    ~CoreAccount() override;

    CoreAccount(const CoreAccount&) = delete;
    CoreAccount& operator=(const CoreAccount&) = delete;

    void shutdown();

signals:
    void messageNotification(const QString& peer, const QString& fromName);
    void reactionNotification(const QString& peer, const QString& fromName, const QString& emoji);

private slots:
    void noteCommandQueued();

private:
    void onCommandFinished();
    void showSlowCommands();
    void open(const QString& file, const QString& passphrase, bool startOnline);
    void close();
    void beginShutdown();
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
    void restartDelivery(const QString& peer, qint64 localId);
    void bumpStatus(qint64 localId, int status);
    void onSendProgress(qint64 localId, int state);
    void onSendResult(qint64 localId, bool ok, const QString& error);
    void onSendPhase(qint64 localId, const QString& phase);
    void noteDeliveredThrough(const QString& peer, qint64 uptoId);
    client::AccountDb& accountDb();

    AccountCommands& commands_;
    AccountEvents& events_;
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
