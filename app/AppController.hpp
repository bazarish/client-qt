// Bazarish project (c) 2026
#pragma once

#include "AppCommands.hpp"
#include "AppEvents.hpp"
#include "Models.hpp"
#include "SessionController.hpp"

#include <bazarish/Address.hpp>
#include <bazarish/Limits.hpp>

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include <filesystem>
#include <utility>

class QQuickItem;
class QQuickWindow;

namespace bazarish::app {

class Relay;
class Translations;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool attached READ attached NOTIFY attachedChanged)
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
    Q_PROPERTY(bool mobile READ mobile CONSTANT)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled
            NOTIFY notificationsEnabledChanged)
    Q_PROPERTY(bool backgroundTasksVisible READ backgroundTasksVisible
            WRITE setBackgroundTasksVisible NOTIFY backgroundTasksVisibleChanged)
    Q_PROPERTY(bool lightTheme READ lightTheme WRITE setLightTheme NOTIFY lightThemeChanged)
    Q_PROPERTY(bool screenCapture READ screenCapture WRITE setScreenCapture
            NOTIFY screenCaptureChanged)
    Q_PROPERTY(qreal keyboardInset READ keyboardInset NOTIFY keyboardInsetChanged)
    // The embedded upstream i2pd engine version (e.g.
    Q_PROPERTY(QString i2pdVersion READ i2pdVersion CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

    Q_PROPERTY(QString ringingPeer READ ringingPeer NOTIFY ringingChanged)
    Q_PROPERTY(QString ringingPeerFingerprint READ ringingPeerFingerprint NOTIFY ringingChanged)
    Q_PROPERTY(QString ringingAccountName READ ringingAccountName NOTIFY ringingChanged)
    Q_PROPERTY(qreal ringLevel READ ringLevel NOTIFY ringLevelChanged)
public:
    explicit AppController(Translations& translations, QObject* parent = nullptr);

    void attach(Relay& relay);
    void detach();
    bool attached() const { return attached_; }
    bool coreClosing() const { return coreClosing_; }

    QObject* accountList() { return &accountList_; }
    QObject* accounts() { return &accounts_; }
    int unreadElsewhere() const;
    static int maxGreetingLength() { return static_cast<int>(bazarish::kMaxContactGreetingBytes); }
    static QString aliasSigil() { return QString(QChar(bazarish::kAliasSigil)); }
    static bool mobile()
    {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        return true;
#else
        return false;
#endif
    }
    QObject* session();
    bool hasAccounts() const { return !accountRows_.isEmpty(); }
    bool hasOpenAccounts() const { return !sessions_.isEmpty(); }
    QString i2pdVersion() const;
    QString appVersion() const;

    // A bazarish:// link handed over by the desktop or by a second copy.
    Q_INVOKABLE void openLink(const QString& link);

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

    bool lightTheme() const { return lightTheme_; }
    void setLightTheme(bool on);
    bool screenCapture() const { return screenCapture_; }
    void setScreenCapture(bool on);
    qreal keyboardInset() const { return keyboardInset_; }
    void setKeyboardInset(qreal inset);

    qreal ringLevel() const { return ringLevel_; }
    QString ringingPeer() const { return ringingPeer_; }
    QString ringingPeerFingerprint() const { return ringingPeerFingerprint_; }
    QString ringingAccountName() const { return ringingAccountName_; }
    Q_INVOKABLE void answerRinging();
    Q_INVOKABLE void declineRinging();
    static std::filesystem::path accountsFolder();
    QVector<AccountRow> accountStatuses() const { return accountStatuses_; }
    void retranslate();

    Q_PROPERTY(bool portable READ portable NOTIFY portableChanged)
    bool portable() const;
    Q_PROPERTY(bool backgroundRestricted READ backgroundRestricted
            NOTIFY backgroundRestrictedChanged)
    bool backgroundRestricted() const;
    Q_INVOKABLE void openBackgroundSettings() const;
    Q_PROPERTY(bool fullScreenCallsRestricted READ fullScreenCallsRestricted
            NOTIFY backgroundRestrictedChanged)
    bool fullScreenCallsRestricted() const;
    Q_INVOKABLE void openFullScreenCallsSettings() const;
    Q_INVOKABLE bool closeTopPopup(QQuickWindow* window) const;
    Q_INVOKABLE void forgetFocusOnClose(QQuickItem* overlay) const;
    Q_PROPERTY(bool callLive READ callLive NOTIFY callLiveChanged)
    bool callLive() const { return callLive_; }
    Q_INVOKABLE void requestUnlocked();
    Q_PROPERTY(bool speakerOn READ speakerOn WRITE setSpeakerOn NOTIFY speakerOnChanged)
    bool speakerOn() const { return speakerOn_; }
    void setSpeakerOn(bool on);
    Q_INVOKABLE void moveToBackground() const;
    Q_INVOKABLE void paintSystemBars(const QColor& color) const;
    Q_PROPERTY(QString dataLocation READ dataLocation NOTIFY portableChanged)
    QString dataLocation() const;
    Q_INVOKABLE void setPortable(bool on);
    Q_INVOKABLE void requestAddAccount();
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE void holdSelection(QObject* field) const;

    Q_INVOKABLE bool clipboardHasImage() const;

    Q_INVOKABLE QString markupHtml(const QString& text, const QColor& actionColor,
        const QColor& chipColor, const QColor& codeColor, const QColor& codeTextColor) const;
    Q_INVOKABLE QString markupPlain(const QString& text) const;
    Q_INVOKABLE int emojiCount(const QString& text) const;

signals:
    void attachedChanged();
    void operationsChanged();
    void pairingChanged();
    void pairingFinished(bool ok);
    void portableChanged();
    void backgroundRestrictedChanged();
    void callLiveChanged();
    void unlocked();
    void speakerOnChanged();
    void restartRequired(const QString& message);
    void accountListChanged();
    void accountsChanged();
    void notificationsEnabledChanged();
    void backgroundTasksVisibleChanged();
    void lightThemeChanged();
    void screenCaptureChanged();
    void keyboardInsetChanged();
    void ringingChanged();
    void ringLevelChanged();
    void raiseRequested();
    void sessionChanged();
    void accountOpened();
    void accountOpenFailed(const QString& error);
    void createFailed(const QString& error);
    void showPicker();
    void needPassphrase(const QString& id, const QString& name);
    void accountDeleteFailed(const QString& id, const QString& error, bool profileNotOpened);
    void deletingChanged();
    void accountDeleteNeedsUnlock(const QString& id, const QString& name);
    void unlockFailed(const QString& error);
    void accountUnlocked(const QString& id);
    void inviteLinkOpened(const QString& link);
    void serverLinkOpened(const QString& link);
    void signLinkOpened(const QString& link);
    void pairLinkOpened(const QString& link);
    void linkRefused(const QString& reason);

private:
    SessionController* sessionFor(const QString& id) const;
    void startSession(const QString& id);
    void endSession(const QString& id);
    void applyPendingLink();

    AppCommands commands_;
    AppEvents events_;
    Relay* relay_ = nullptr;
    bool attached_ = false;
    bool coreClosing_ = false;
    bool answerWhenRinging_ = false;
    AccountListModel accountList_;
    QVector<AccountListRow> accountRows_;
    OpenAccountsModel accounts_;
    QVector<AccountRow> accountStatuses_;
    OperationListModel operations_;
    QList<SessionController*> sessions_;
    QString activeId_;
    // A link that arrived before any account was open waits for one.
    QString pendingLink_;
    std::pair<QString, QString> pendingConversation_;
    QString deletingId_;
    bool notifications_ = true;
    bool backgroundTasks_ = false;
    bool lightTheme_ = true;
    bool screenCapture_ = false;
    qreal keyboardInset_ = 0;
    QString ringingAccount_;
    QString ringingPeer_;
    QString ringingPeerFingerprint_;
    QString ringingAccountName_;
    qreal ringLevel_ = 0.0;
    bool callLive_ = false;
    bool speakerOn_ = false;
    QString pairStatus_;
    double pairProgress_ = kProgressUnknown;
    bool pairNeedsCode_ = false;
    bool pairing_ = false;
};

}  // namespace bazarish::app
