// Bazarish project (c) 2026
#pragma once

#include "AccountCommands.hpp"
#include "AccountEvents.hpp"
#include "ContactBook.hpp"
#include "MessageIngest.hpp"
#include "TranscriptStore.hpp"

#include <QHash>
#include <QObject>
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

private:
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
    QString viewedPeer_;
    bool startOnline_ = true;
    bool online_ = false;
    bool sendReceipts_ = true;
    QHash<QString, qint64> lastReadAckedId_;
    QHash<QString, qint64> pendingReadSync_;
    QTimer readSyncTimer_;
};

}  // namespace bazarish::app
