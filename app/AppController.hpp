// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "AccountManager.hpp"
#include "Ringtone.hpp"
#include "SessionController.hpp"

#include "DevicePairing.hpp"

#include <bazarish/Address.hpp>
#include <bazarish/Limits.hpp>

#include <QList>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <filesystem>
#include <memory>

namespace bazarish::app {

// Owns the account manager and every open account.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* operations READ operations CONSTANT)
    Q_PROPERTY(QString pairStatus READ pairStatus NOTIFY pairingChanged)
    Q_PROPERTY(double pairProgress READ pairProgress NOTIFY pairingChanged)
    Q_PROPERTY(bool pairNeedsCode READ pairNeedsCode NOTIFY pairingChanged)
    Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
    Q_PROPERTY(int activeOperations READ activeOperations NOTIFY operationsChanged)
    Q_PROPERTY(QObject* accountList READ accountList CONSTANT)
    Q_PROPERTY(QObject* accounts READ accounts CONSTANT)
    Q_PROPERTY(QObject* session READ session NOTIFY sessionChanged)
    Q_PROPERTY(bool hasAccounts READ hasAccounts NOTIFY accountListChanged)
    Q_PROPERTY(bool hasOpenAccounts READ hasOpenAccounts NOTIFY accountsChanged)
    Q_PROPERTY(int unreadElsewhere READ unreadElsewhere NOTIFY accountsChanged)
    Q_PROPERTY(int maxGreetingLength READ maxGreetingLength CONSTANT)
    Q_PROPERTY(QString aliasSigil READ aliasSigil CONSTANT)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled
            NOTIFY notificationsEnabledChanged)
    Q_PROPERTY(bool backgroundTasksVisible READ backgroundTasksVisible
            WRITE setBackgroundTasksVisible NOTIFY backgroundTasksVisibleChanged)
    // The embedded upstream i2pd engine version (e.g.
    Q_PROPERTY(QString i2pdVersion READ i2pdVersion CONSTANT)

    Q_PROPERTY(QString ringingPeer READ ringingPeer NOTIFY ringingChanged)
    Q_PROPERTY(QString ringingPeerFingerprint READ ringingPeerFingerprint NOTIFY ringingChanged)
    Q_PROPERTY(QString ringingAccountName READ ringingAccountName NOTIFY ringingChanged)
    Q_PROPERTY(qreal ringLevel READ ringLevel NOTIFY ringLevelChanged)
public:
    explicit AppController(QObject* parent = nullptr);

    QObject* accountList() { return &accountList_; }
    QObject* accounts() { return &accounts_; }
    int unreadElsewhere() const;
    static int maxGreetingLength() { return static_cast<int>(bazarish::kMaxContactGreetingBytes); }
    static QString aliasSigil() { return QString(QChar(bazarish::kAliasSigil)); }
    QObject* session();
    bool hasAccounts() const { return haveAccounts_; }
    bool hasOpenAccounts() const { return !sessions_.isEmpty(); }
    QString i2pdVersion() const;

    Q_INVOKABLE void refreshAccountList();
    Q_INVOKABLE void createAccount(const QString& name, const QString& passphrase);
    Q_INVOKABLE void openAccount(const QString& id, const QString& passphrase);
    Q_INVOKABLE void cancelUnlock();
    QObject* operations() { return &operations_; }
    int activeOperations() const { return operations_.runningCount(); }

    Q_INVOKABLE void importAccount(const QString& name, const QString& fileUrl,
        const QString& password, const QString& atRestPassphrase);
    Q_INVOKABLE QString pairLinkProblem(const QString& link) const;
    Q_INVOKABLE void startPairing(const QString& link, const QString& atRestPassphrase);
    Q_INVOKABLE void submitPairCode(const QString& code);
    Q_INVOKABLE void cancelPairing();
    QString pairStatus() const { return pairStatus_; }
    double pairProgress() const { return pairProgress_; }
    bool pairNeedsCode() const { return pairNeedsCode_; }
    bool pairing() const { return pairing_; }
    Q_INVOKABLE void prepareForExit();
    Q_INVOKABLE void closeAllSessions();

    Q_INVOKABLE void deleteAccount(const QString& id);
    Q_PROPERTY(QString deletingId READ deletingId NOTIFY deletingChanged)
    QString deletingId() const { return deletingId_; }
    Q_INVOKABLE void deleteAccountAfterUnlock(const QString& id);
    Q_INVOKABLE void forgetAccountLocally(const QString& id);
    Q_INVOKABLE void switchTo(const QString& id);
    void openConversationOf(const QString& accountId, const QString& peer);
    Q_INVOKABLE void setOnline(const QString& id, bool on);

    bool notificationsEnabled() const { return notifications_; }
    void setNotificationsEnabled(bool on);

    bool backgroundTasksVisible() const { return backgroundTasks_; }
    void setBackgroundTasksVisible(bool on);

    qreal ringLevel() const { return ringLevel_; }
    QString ringingPeer() const { return ringingPeer_; }
    QString ringingPeerFingerprint() const { return ringingPeerFingerprint_; }
    QString ringingAccountName() const { return ringingAccountName_; }
    Q_INVOKABLE void answerRinging();
    Q_INVOKABLE void declineRinging();
    static std::filesystem::path accountsFolder();
    static QString soundFolder();
    QVector<AccountRow> accountStatuses() const { return accountStatuses_; }
    void rebuildI2pLinks();
    void retranslate();

    Q_PROPERTY(bool portable READ portable NOTIFY portableChanged)
    bool portable() const;
    Q_PROPERTY(QString dataLocation READ dataLocation NOTIFY portableChanged)
    QString dataLocation() const;
    Q_INVOKABLE void setPortable(bool on);
    Q_INVOKABLE void requestAddAccount();
    Q_INVOKABLE void copyText(const QString& text) const;

    Q_INVOKABLE bool clipboardHasImage() const;

    Q_INVOKABLE QString markupHtml(const QString& text, const QColor& actionColor,
        const QColor& chipColor, const QColor& codeColor, const QColor& codeTextColor) const;
    Q_INVOKABLE QString markupPlain(const QString& text) const;

signals:
    void operationsChanged();
    void pairingChanged();
    void pairingFinished(bool ok);
    void portableChanged();
    void restartRequired(const QString& message);
    void accountListChanged();
    void accountsChanged();
    void notificationsEnabledChanged();
    void backgroundTasksVisibleChanged();
    void ringingChanged();
    void ringLevelChanged();
    void raiseRequested();
    void notificationRequested(const QString& accountId, const QString& peer,
        const QString& title, const QString& body);
    void reactionNotificationRequested(const QString& accountId, const QString& peer,
        const QString& title, const QString& body);
    void sessionChanged();
    void accountOpened();
    void accountOpenFailed(const QString& error);
    void createFailed(const QString& error);
    void showPicker();
    void needPassphrase(const QString& id, const QString& name);
    void accountDeleteFailed(const QString& id, const QString& error, bool profileNotOpened);
    void deletingChanged();
    void readyToExit();
    void accountDeleteNeedsUnlock(const QString& id, const QString& name);
    void unlockFailed(const QString& error);
    void accountUnlocked(const QString& id);

private:
    SessionController* sessionFor(const QString& id) const;
    SessionController* activeController() const;
    void openSession(const QString& id, const QString& passphrase, bool makeActive,
        bool startOnline = true);
    void openAllAccounts();
    void removeSession(SessionController* ctrl);
    void onSessionClosed(const QString& id);
    void removeAccountFiles(const QString& id);
    void refreshAccounts();
    void refreshAccountRows();
    void setActive(const QString& id);
    QString readLastActive() const;
    void writeLastActive(const QString& id) const;

    void loadOfflineSet();
    void persistOfflineSet() const;
    void setAccountOffline(const QString& id, bool offline);

    void loadSettings();
    void persistSettings() const;

    std::unique_ptr<client::AccountManager> manager_;
    void failPairing(const QString& reason);
    QString pairStatus_;
    double pairProgress_ = kProgressUnknown;
    bool pairNeedsCode_ = false;
    bool pairing_ = false;
    QString pairDest_;
    QString pairAtRest_;
    std::shared_ptr<std::atomic<bool>> pairCancel_;
    std::shared_ptr<bazarish::i2p::Endpoint> pairEndpoint_;
    AccountListModel accountList_;
    QVector<AccountListRow> accountRows_;
    QString notificationTitle(const SessionController* ctrl) const;

    OpenAccountsModel accounts_;
    QVector<AccountRow> accountStatuses_;
    bool notifications_ = true;
    bool backgroundTasks_ = false;

    void updateRinging();
    Ringtone ringtone_;
    QString ringingAccount_;
    QString ringingPeer_;
    QString ringingPeerFingerprint_;
    QString ringingAccountName_;
    qreal ringLevel_ = 0.0;
    QList<SessionController*> sessions_;
    QString activeId_;
    bool haveAccounts_ = false;
    QSet<QString> offline_;
    QString unlockingId_;
    QString pendingDeleteId_;
    QString deletingId_;
    QSet<QString> pendingRemovals_;
    bool exiting_ = false;
    int closingCount_ = 0;
    bool unlockToBringOnline_ = false;
    OperationListModel operations_;
};

}  // namespace bazarish::app
