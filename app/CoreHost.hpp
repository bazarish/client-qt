// Bazarish project (c) 2026
#pragma once

#include "AccountManager.hpp"
#include "AppCommands.hpp"
#include "AppEvents.hpp"
#include "I2pService.hpp"
#include "OperationLog.hpp"
#include "Ringtone.hpp"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

namespace bazarish::i2p {
class Endpoint;
}

namespace bazarish::app {

class CoreAccount;
class Relay;

class CoreHost : public QObject {
    Q_OBJECT
public:
    explicit CoreHost(QObject* parent = nullptr);
    ~CoreHost() override;

    void attach(Relay& relay);
    void start();

    QVector<AccountRow> accountStatuses() const { return accountStatuses_; }
    bool notificationsEnabled() const { return notifications_; }
    void setNotificationsEnabled(bool on);
    QString ringingAccount() const { return ringingAccount_; }
    QString ringingPeer() const { return ringingPeer_; }
    QString ringingPeerFingerprint() const { return ringingPeerFingerprint_; }
    bool callLive() const { return callLive_; }
    bool callTalking() const { return callTalking_; }
    bool speakerOn() const { return speakerOn_; }
    bool windowActive() const { return windowActive_; }
    void prepareForExit();
    static QString soundFolder();

signals:
    void accountsChanged();
    void notificationsEnabledChanged();
    void ringingChanged();
    void callLiveChanged();
    void speakerOnChanged();
    void notificationRequested(const QString& accountId, const QString& peer,
        const QString& title, const QString& body);
    void reactionNotificationRequested(const QString& accountId, const QString& peer,
        const QString& title, const QString& body);
    void readyToExit();

private:
    CoreAccount* sessionFor(const QString& id) const;
    void openSession(const QString& id, const QString& passphrase, bool makeActive,
        bool startOnline = true);
    void openAllAccounts();
    void removeSession(CoreAccount* account);
    void onSessionClosed(const QString& id);
    void removeAccountFiles(const QString& id);
    void refreshAccountList();
    void refreshAccountRows();
    void refreshAccounts();
    void setActive(const QString& id);
    void loadOfflineSet();
    void persistOfflineSet() const;
    void setAccountOffline(const QString& id, bool offline);
    QString notificationTitle(const CoreAccount* account) const;
    void updateRinging();
    void setSpeakerOn(bool on);
    void setBackgroundTasksVisible(bool on);
    void createAccount(const QString& name, const QString& passphrase);
    void openAccount(const QString& id, const QString& passphrase);
    void switchTo(const QString& id);
    void setOnline(const QString& id, bool on);
    void importAccount(const QString& name, const QString& path, const QString& password,
        const QString& atRestPassphrase);
    void startPairing(const QString& link, const QString& atRestPassphrase);
    void submitPairCode(const QString& code);
    void cancelPairing();
    void failPairing(const QString& reason);
    void announcePairing();
    void deleteAccount(const QString& id);
    void deleteAccountAfterUnlock(const QString& id);
    void forgetAccountLocally(const QString& id);
    void cancelUnlock();
    void setDeleting(const QString& id);
    void setPortable(bool on);
    void closeAllSessions();
    void rebuildI2pLinks();

    AppCommands commands_;
    AppEvents events_;
    I2pService i2p_;
    Relay* relay_ = nullptr;
    std::unique_ptr<client::AccountManager> manager_;
    QVector<AccountListRow> accountRows_;
    QVector<AccountRow> accountStatuses_;
    QList<CoreAccount*> sessions_;
    QString activeId_;
    QSet<QString> offline_;
    QString unlockingId_;
    bool unlockToBringOnline_ = false;
    QString pendingDeleteId_;
    QString deletingId_;
    QSet<QString> pendingRemovals_;
    bool exiting_ = false;
    bool windowActive_ = false;
    bool notifications_ = true;
    bool backgroundTasks_ = false;
    Ringtone ringtone_;
    QString ringingAccount_;
    QString ringingPeer_;
    QString ringingPeerFingerprint_;
    QString ringingAccountName_;
    bool callLive_ = false;
    bool callTalking_ = false;
    bool speakerOn_ = false;
    QString pairStatus_;
    double pairProgress_ = kProgressUnknown;
    bool pairNeedsCode_ = false;
    bool pairing_ = false;
    QString pairDest_;
    QString pairAtRest_;
    std::shared_ptr<std::atomic<bool>> pairCancel_;
    std::shared_ptr<bazarish::i2p::Endpoint> pairEndpoint_;
    OperationLog log_;
};

}  // namespace bazarish::app
