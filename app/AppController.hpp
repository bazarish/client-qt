// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "AccountManager.hpp"
#include "Ringtone.hpp"
#include "SessionController.hpp"

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

// Owns the account manager and every open account. Several can be open at once,
// each with its own SessionController, so all of them receive; one is "active",
// meaning the window is bound to it.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* operations READ operations CONSTANT)
    Q_PROPERTY(int activeOperations READ activeOperations NOTIFY operationsChanged)
    Q_PROPERTY(QObject* accountList READ accountList CONSTANT)
    Q_PROPERTY(QObject* accounts READ accounts CONSTANT)
    Q_PROPERTY(QObject* session READ session NOTIFY sessionChanged)
    Q_PROPERTY(bool hasAccounts READ hasAccounts NOTIFY accountListChanged)
    Q_PROPERTY(bool hasOpenAccounts READ hasOpenAccounts NOTIFY accountsChanged)
    // Unread waiting in accounts other than the one on screen: the switcher is
    // the only place they would ever be noticed.
    Q_PROPERTY(int unreadElsewhere READ unreadElsewhere NOTIFY accountsChanged)
    // What a contact request leaves for a person to write: the protocol's cap,
    // so the field cannot be filled past what the recipient's server accepts.
    Q_PROPERTY(int maxGreetingLength READ maxGreetingLength CONSTANT)
    // --- Global (app-wide) settings, shared by every account ---
    // Popup notifications (with their sound), on unless the user turns them off.
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled
            NOTIFY notificationsEnabledChanged)
    // Off by default: what the client is doing behind a conversation is worth
    // watching while something is wrong, and clutter the rest of the time.
    Q_PROPERTY(bool backgroundTasksVisible READ backgroundTasksVisible
            WRITE setBackgroundTasksVisible NOTIFY backgroundTasksVisibleChanged)
    // The embedded upstream i2pd engine version (e.g. "2.60.0"), for display.
    Q_PROPERTY(QString i2pdVersion READ i2pdVersion CONSTANT)

    // --- The call that is ringing now ---
    //
    // Never a tray popup: a popup fades on its own, and a call that faded is a
    // call missed. Empty unless a call is ringing and notifications are on.
    Q_PROPERTY(QString ringingPeer READ ringingPeer NOTIFY ringingChanged)
    Q_PROPERTY(QString ringingPeerFingerprint READ ringingPeerFingerprint NOTIFY ringingChanged)
    // The account being called: the heading of the call window, which has no
    // title bar of the desktop's to carry it.
    Q_PROPERTY(QString ringingAccountName READ ringingAccountName NOTIFY ringingChanged)
    // The ringtone's loudness right now, 0 to 1, so the call window's pulse is the
    // sound itself rather than a timer beside it. Zero when nothing is ringing.
    Q_PROPERTY(qreal ringLevel READ ringLevel NOTIFY ringLevelChanged)
public:
    explicit AppController(QObject* parent = nullptr);

    QObject* accountList() { return &accountList_; }
    QObject* accounts() { return &accounts_; }
    int unreadElsewhere() const;
    static int maxGreetingLength() { return static_cast<int>(bazarish::kMaxContactGreetingBytes); }
    QObject* session();
    bool hasAccounts() const { return haveAccounts_; }
    bool hasOpenAccounts() const { return !sessions_.isEmpty(); }
    QString i2pdVersion() const;

    Q_INVOKABLE void refreshAccountList();
    Q_INVOKABLE void createAccount(const QString& name, const QString& passphrase);
    Q_INVOKABLE void openAccount(const QString& id, const QString& passphrase);
    // The unlock prompt was dismissed, so whatever waited on it does not happen:
    // an account asked to come online stays off.
    Q_INVOKABLE void cancelUnlock();
    // Work that has no session yet, such as restoring from a backup: slow enough
    // that without a row the button it came from reads as broken.
    QObject* operations() { return &operations_; }
    int activeOperations() const { return operations_.runningCount(); }

    Q_INVOKABLE void importAccount(const QString& name, const QString& fileUrl,
        const QString& password, const QString& atRestPassphrase);
    // Says when the application may end. Nothing is torn down and nothing is
    // waited for: asked for by the tray's Quit, where the interface must not sit
    // out a request that is meant to hang for half a minute.
    Q_INVOKABLE void prepareForExit();
    // Closes every open account without waiting. What a transport change uses: the
    // engine is settled at start, so an account left open still rides the old one.
    Q_INVOKABLE void closeAllSessions();

    // Ends the account for good: the server drops the registration, the
    // destination and the mailbox, and only then is the profile removed from this
    // device. An account that is not open is opened first - its identity key is
    // what authorises the deletion - which may ask for its passphrase. A server
    // that refused or could not be reached leaves everything as it was and
    // reports accountDeleteFailed: the profile is the only thing that can ask
    // again, so it is never thrown away on a failure.
    Q_INVOKABLE void deleteAccount(const QString& id);
    // The account being deleted right now, empty when none is. Deleting waits on
    // the server, so until it answers the button says so and a second press is a
    // no-op.
    Q_PROPERTY(QString deletingId READ deletingId NOTIFY deletingChanged)
    QString deletingId() const { return deletingId_; }
    // Asks for a locked profile's passphrase and deletes it once it opens.
    Q_INVOKABLE void deleteAccountAfterUnlock(const QString& id);
    // Removes the profile from this device and nothing else: the account lives on
    // at its server with no key left anywhere to end it. Offered only when a full
    // deletion cannot be made.
    Q_INVOKABLE void forgetAccountLocally(const QString& id);
    // Makes an account the active (focused) one, opening it first if needed.
    // An encrypted, not-yet-open account emits needPassphrase instead.
    Q_INVOKABLE void switchTo(const QString& id);
    // Opens one conversation, switching the window to its account if need be.
    // Which account is on screen is display only: no key is touched by it.
    void openConversationOf(const QString& accountId, const QString& peer);
    // Brings a specific account online (open + sync) or offline (stop syncing),
    // independently of which account is active. Bringing an encrypted, unopened
    // account online emits needPassphrase.
    Q_INVOKABLE void setOnline(const QString& id, bool on);

    bool notificationsEnabled() const { return notifications_; }
    void setNotificationsEnabled(bool on);

    bool backgroundTasksVisible() const { return backgroundTasks_; }
    void setBackgroundTasksVisible(bool on);

    qreal ringLevel() const { return ringLevel_; }
    QString ringingPeer() const { return ringingPeer_; }
    QString ringingPeerFingerprint() const { return ringingPeerFingerprint_; }
    QString ringingAccountName() const { return ringingAccountName_; }
    // Answers the ringing call, making its account active and raising the window:
    // the microphone, the levels and hanging up all live in there.
    Q_INVOKABLE void answerRinging();
    Q_INVOKABLE void declineRinging();
    // The folder every account lives in. Static: the application decides whether it
    // may run at all by this path, before anything is opened.
    static std::filesystem::path accountsFolder();
    // The root of the installation, one above the accounts: the global settings
    // and the sounds of the user's own are there rather than among the accounts.
    static QString soundFolder();
    // The account rows behind the model, for the tray menu: name, status, unread.
    QVector<AccountRow> accountStatuses() const { return accountStatuses_; }
    // Rebuilds every open account's I2P destinations, so a tunnel-profile change
    // reaches the ones already up and not only the next one built.
    void rebuildI2pLinks();

    // Everything the app keeps lives beside the executable rather than in the
    // user's data directory. Switching moves the data, and the embedded router
    // holds its directory for the life of the process, so a restart follows.
    Q_PROPERTY(bool portable READ portable NOTIFY portableChanged)
    bool portable() const;
    Q_PROPERTY(QString dataLocation READ dataLocation NOTIFY portableChanged)
    QString dataLocation() const;
    Q_INVOKABLE void setPortable(bool on);
    // Asks the UI to show the picker so another account can be added, without
    // closing the open ones.
    Q_INVOKABLE void requestAddAccount();
    // Signs out (closes) the active account; switches to another if any remain.
    Q_INVOKABLE void closeAccount();
    // A writable path for a short-lived working file. QML resolves relative names
    // against the read-only qrc bundle, so the location has to come from here.
    Q_INVOKABLE QString scratchFile(const QString& name) const;
    // Puts text on the system clipboard. Here rather than on a session because
    // the account picker copies a fingerprint before any account is open.
    Q_INVOKABLE void copyText(const QString& text) const;

    // --- Images: the one attachment worth showing rather than listing, and worth
    // shrinking before it crosses I2P ---

    // Scales and re-encodes a picture to something a tunnel can carry. Returns a
    // file:// URL of the prepared copy, or empty when the file is not a picture
    // this can read - which it reports through imageRejected.
    Q_INVOKABLE QString prepareImageForSend(const QString& fileUrl);
    // The same for whatever the clipboard holds.
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE QString prepareClipboardImage();

    // --- Message text ---
    //
    // A body is drawn as a rich text document, built here and not in QML: the text
    // is a correspondent's, and writing the tags ourselves keeps theirs out.

    // A body as rich text. The document carries no palette of its own, so every
    // colour it is drawn with comes from the caller's theme.
    Q_INVOKABLE QString markupHtml(const QString& text, const QColor& actionColor,
        const QColor& chipColor, const QColor& codeColor, const QColor& codeTextColor) const;
    // The same body with the markers taken out: what a preview, a reply quote or
    // a search hit shows.
    Q_INVOKABLE QString markupPlain(const QString& text) const;

private:
    // Encodes a prepared picture into the scratch directory and returns its URL.
    QString writePreparedImage(const QImage& image, const QString& baseName);

public:

signals:
    void operationsChanged();
    // A picture the user chose could not be prepared: not an image, or unreadable.
    void imageRejected(const QString& reason);
    void portableChanged();
    // The data moved; the app must be started again to use it.
    void restartRequired(const QString& message);
    void accountListChanged();
    void accountsChanged();
    void notificationsEnabledChanged();
    void backgroundTasksVisibleChanged();
    void ringingChanged();
    void ringLevelChanged();
    // Asks the window to come forward (answering a call from outside it).
    void raiseRequested();
    // What a tray popup needs to be shown and to be clickable: which account and
    // which conversation it came from, then what to put on it.
    void notificationRequested(const QString& accountId, const QString& peer,
        const QString& title, const QString& body);
    void reactionNotificationRequested(const QString& accountId, const QString& peer,
        const QString& title, const QString& body);
    void sessionChanged();
    void accountOpened();
    void accountOpenFailed(const QString& error);
    void createFailed(const QString& error);
    void showPicker();
    // An encrypted account needs its passphrase before it can be opened.
    void needPassphrase(const QString& id, const QString& name);
    // The account could not be ended on its server, so nothing was deleted.
    // profileNotOpened separates "the server did not answer" from "this profile
    // would not open at all": the first can be tried again, the second cannot,
    // and only one of them leaves anything to say about the server.
    void accountDeleteFailed(const QString& id, const QString& error, bool profileNotOpened);
    // The profile is locked, and ending the account on its server needs the key
    // inside it. The UI offers unlocking it or deleting this device's copy alone.
    void deletingChanged();
    // Every account is closed (or the grace ran out): the process may end.
    void readyToExit();
    void accountDeleteNeedsUnlock(const QString& id, const QString& name);
    // An unlock attempt failed. It belongs on the unlock screen, where the
    // passphrase was typed, and not in a notice at the bottom of the window.
    void unlockFailed(const QString& error);
    // An account opened with the passphrase just typed. The prompt closes on this
    // and nothing else: pressing the button is not the same as being let in.
    void accountUnlocked(const QString& id);

private:
    SessionController* sessionFor(const QString& id) const;
    SessionController* activeController() const;
    // Opens an account into a SessionController. An encrypted one given an empty
    // passphrase is reported through needPassphrase. startOnline false opens it to
    // be read, not switched on: only the account list's switch does that.
    void openSession(const QString& id, const QString& passphrase, bool makeActive,
        bool startOnline = true);
    // Opens every unencrypted account in the background at startup.
    void openAllAccounts();
    // Takes a session out of the interface, closes the account it holds and
    // hands the object itself to Qt to destroy once the current signal is done.
    void removeSession(SessionController* ctrl);
    // A session finished closing: what was waiting on it happens here.
    void onSessionClosed(const QString& id);
    // Takes the account's files off the disk. Only ever called once its session
    // has let go of them.
    void removeAccountFiles(const QString& id);
    // Closes every open account, joining their workers - so nothing is holding a
    // transcript open while the data directory moves.
    void refreshAccounts();
    // Patches the listed accounts with what the open sessions know.
    void refreshAccountRows();
    void setActive(const QString& id);
    // The last active account is remembered across runs (a file under the
    // accounts root), so the app reopens straight into it with no picker.
    QString readLastActive() const;
    void writeLastActive(const QString& id) const;

    // Accounts the user turned off are remembered across runs and not auto-opened,
    // so a disabled account stays off. Read once at construction.
    void loadOfflineSet();
    void persistOfflineSet() const;
    void setAccountOffline(const QString& id, bool offline);

    // Global settings (a small JSON file under the accounts root), loaded once at
    // construction and persisted on change. Currently just full privacy mode.
    void loadSettings();
    void persistSettings() const;

    std::unique_ptr<client::AccountManager> manager_;
    AccountListModel accountList_;
    // The accounts found on disk at the last refresh. The list is rebuilt on every
    // unread change, and reopening a database to do it would re-run the key
    // derivation each time.
    QVector<AccountListRow> accountRows_;
    // The bold line of a tray popup: which account the event reached, said only
    // when more than one is open.
    QString notificationTitle(const SessionController* ctrl) const;

    OpenAccountsModel accounts_;
    QVector<AccountRow> accountStatuses_;
    bool notifications_ = true;
    bool backgroundTasks_ = false;

    // Recomputed from every open account whenever a call state or the
    // notification setting changes; it also starts and stops the ringtone.
    void updateRinging();
    Ringtone ringtone_;
    QString ringingAccount_;
    QString ringingPeer_;
    QString ringingPeerFingerprint_;
    QString ringingAccountName_;
    qreal ringLevel_ = 0.0;
    // The call state each account was last seen in, so a call is announced when it
    // starts ringing and not again on every tick that follows.
    QList<SessionController*> sessions_;  // open accounts, owned (parented here)
    QString activeId_;
    bool haveAccounts_ = false;
    // Ids of accounts the user turned offline (persisted; not auto-opened).
    QSet<QString> offline_;
    // The account an unlock prompt is open for, and whether it was asked for in
    // order to bring the account online rather than only to read it.
    QString unlockingId_;
    // An account unlocked for the sole purpose of deleting it.
    QString pendingDeleteId_;
    // The account whose deletion is under way, and the accounts whose files are
    // waiting for their session to close.
    QString deletingId_;
    QSet<QString> pendingRemovals_;
    bool exiting_ = false;
    // How many sessions are still closing.
    int closingCount_ = 0;
    bool unlockToBringOnline_ = false;
    // Account-level activity (a restore in progress).
    OperationListModel operations_;
};

}  // namespace bazarish::app
