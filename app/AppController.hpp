// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "AccountManager.hpp"
#include "SessionController.hpp"

#include <bazarish/Limits.hpp>

#include <QHash>
#include <QList>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <filesystem>
#include <memory>

namespace bazarish::app {

// Root application object: owns the account manager and the set of currently
// open accounts. Several accounts can be open at once - each keeps its own
// SessionController (worker thread + background sync), so all of them receive -
// and one is "active" (the one the UI is bound to). Drives the launch flow
// (pick or create an account, then unlock it) and account switching/removal.
class AppController : public QObject {
    Q_OBJECT
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
    // Full privacy mode: forbid connecting through any clearnet client-facade, so
    // all traffic runs over I2P only. Persisted across runs and applied process-wide.
    // Popup notifications (with their sound), on unless the user turns them off.
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled
            NOTIFY notificationsEnabledChanged)
    Q_PROPERTY(bool fullPrivacyMode READ fullPrivacyMode WRITE setFullPrivacyMode
            NOTIFY fullPrivacyModeChanged)
    // The embedded upstream i2pd engine version (e.g. "2.60.0"), for display.
    Q_PROPERTY(QString i2pdVersion READ i2pdVersion CONSTANT)
public:
    explicit AppController(QObject* parent = nullptr);

    QObject* accountList() { return &accountList_; }
    QObject* accounts() { return &accounts_; }
    int unreadElsewhere() const;
    // Hands the core every clearnet facade this application knows of, from all
    // open accounts. Bootstrapping I2P is the application's job: with three
    // accounts there are three servers to ask before reaching for a public
    // reseed host.
    void publishReseedFacades();
    static int maxGreetingLength() { return static_cast<int>(bazarish::kMaxContactGreetingBytes); }
    QObject* session();
    bool hasAccounts() const { return haveAccounts_; }
    bool hasOpenAccounts() const { return !sessions_.isEmpty(); }
    bool fullPrivacyMode() const { return fullPrivacy_; }
    void setFullPrivacyMode(bool on);
    QString i2pdVersion() const;

    Q_INVOKABLE void refreshAccountList();
    Q_INVOKABLE void createAccount(const QString& name, const QString& passphrase);
    Q_INVOKABLE void openAccount(const QString& id, const QString& passphrase);
    // The unlock prompt was dismissed. Whatever was waiting on it does not
    // happen: an account asked to come online stays off, and its switch goes
    // back to what is on disk.
    Q_INVOKABLE void cancelUnlock();
    Q_INVOKABLE void importAccount(const QString& name, const QString& fileUrl,
        const QString& password, const QString& atRestPassphrase);
    // Ends the account for good: the server drops the registration, the
    // destination and the mailbox, and only then is the profile removed from this
    // device. An account that is not open is opened first (its identity key is
    // what authorises the deletion), which may ask for its passphrase. A server
    // that refused or could not be reached leaves everything as it was and
    // reports accountDeleteFailed - the profile is the only thing that can ask
    // again, so it is never thrown away on a failure.
    Q_INVOKABLE void deleteAccount(const QString& id);
    // Asks for a locked profile's passphrase and deletes it once it opens.
    Q_INVOKABLE void deleteAccountAfterUnlock(const QString& id);
    // Removes the profile from this device and nothing else: the account goes on
    // existing on its server, with its destination and its mail, and no key left
    // anywhere to end it. Offered when a full deletion cannot be made - a locked
    // profile the user will not unlock, or a server that did not answer.
    Q_INVOKABLE void forgetAccountLocally(const QString& id);
    // Makes an account the active (focused) one, opening it first if needed.
    // An encrypted, not-yet-open account emits needPassphrase instead.
    Q_INVOKABLE void switchTo(const QString& id);
    // Brings a specific account online (open + sync) or offline (stop syncing),
    // independently of which account is active. Bringing an encrypted, unopened
    // account online emits needPassphrase.
    Q_INVOKABLE void setOnline(const QString& id, bool on);

    bool notificationsEnabled() const { return notifications_; }
    void setNotificationsEnabled(bool on);
    // The folder every account, the global settings and a notification sound of the
    // user's own live in. Static: the application decides whether it may run at all
    // by this path, before anything is opened.
    static std::filesystem::path accountsFolder();
    QString soundFolder() const;
    // The account rows behind the model, for the tray menu: name, status, unread.
    QVector<AccountRow> accountStatuses() const { return accountStatuses_; }
    // Rebuilds every open account's I2P destinations, so a change of tunnel
    // profile reaches destinations that are already up instead of only the next
    // one built.
    void rebuildI2pLinks();

    // Portable mode: everything this app keeps lives beside the executable
    // instead of in the user's data directory. Switching moves the data, and the
    // embedded I2P router holds its directory for the life of the process, so the
    // app has to be started again afterwards.
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
    // A writable path for a short-lived working file (the cropped avatar on its
    // way to the compressor). QML resolves relative names against the qrc bundle,
    // which is read-only, so the location has to come from here.
    Q_INVOKABLE QString scratchFile(const QString& name) const;

    // --- Images ---
    //
    // A picture is the one attachment worth showing rather than listing, and the
    // one worth shrinking before it crosses I2P.

    // Prepares a picture for sending: reads it, scales it down to something a
    // tunnel can carry and re-encodes it. Returns a file:// URL of the prepared
    // copy in the scratch directory, or an empty string when the file is not a
    // picture this can read (and reports why).
    Q_INVOKABLE QString prepareImageForSend(const QString& fileUrl);
    // The same for whatever the clipboard holds.
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE QString prepareClipboardImage();

    // A file:// URL for a picture that is safe to render, or an empty string.
    // What decides is the file's own first bytes, never the name or the type the
    // sender claimed: a bubble must not run a renderer over whatever arrived
    // because the other side called it a PNG.
    Q_INVOKABLE QString imageUrlIfSafe(const QString& localPath) const;
    // Whether an attachment is worth trying to show inline at all, by the type
    // the sender declared. The bytes still decide (imageUrlIfSafe).
    Q_INVOKABLE bool looksLikeImage(const QString& mime) const;

private:
    // Encodes a prepared picture into the scratch directory and returns its URL.
    QString writePreparedImage(const QImage& image, const QString& baseName);

public:

signals:
    // A picture the user chose could not be prepared: not an image, or unreadable.
    void imageRejected(const QString& reason);
    void portableChanged();
    // The data moved; the app must be started again to use it.
    void restartRequired(const QString& message);
    void accountListChanged();
    void accountsChanged();
    void notificationsEnabledChanged();
    // Worth telling the user about even when they are not looking: an arrived
    // message, an incoming call.
    void notificationRequested(const QString& title, const QString& body);
    void sessionChanged();
    void fullPrivacyModeChanged();
    void accountOpened();
    void accountOpenFailed(const QString& error);
    void createFailed(const QString& error);
    void showPicker();
    // An encrypted account needs its passphrase before it can be opened.
    void needPassphrase(const QString& id, const QString& name);
    // The account could not be ended on its server, so nothing was deleted.
    void accountDeleteFailed(const QString& id, const QString& error);
    // The profile is locked, and ending the account on its server needs the key
    // inside it. The UI offers unlocking it or deleting this device's copy alone.
    void accountDeleteNeedsUnlock(const QString& id, const QString& name);
    // An unlock attempt failed. It belongs on the unlock screen, where the
    // passphrase was typed, and not in a notice at the bottom of the window.
    void unlockFailed(const QString& error);
    // An account opened with the passphrase that was just typed. The prompt closes
    // on this and on nothing else: pressing the button is not the same as being
    // let in.
    void accountUnlocked(const QString& id);

private:
    SessionController* sessionFor(const QString& id) const;
    SessionController* activeController() const;
    // Opens an account into a SessionController. makeActive focuses it (and
    // routes the UI to it); pass it false to open in the background. Encrypted
    // accounts opened with an empty passphrase are reported via needPassphrase.
    void openSession(const QString& id, const QString& passphrase, bool makeActive);
    // Opens every unencrypted account in the background at startup.
    void openAllAccounts();
    // Removes and tears down an open account's controller. deferred uses
    // deleteLater (required when called from within the controller's own
    // signal); otherwise the controller is destroyed synchronously, so its
    // transcript is flushed and closed before any on-disk removal.
    void removeSession(SessionController* ctrl, bool deferred);
    // Closes every open account, joining their workers - so nothing is holding a
    // transcript open while the data directory moves.
    void closeAllSessions();
    void refreshAccounts();
    // Patches the listed accounts with what the open sessions know.
    void refreshAccountRows();
    void setActive(const QString& id);
    // The last active account is remembered across runs (a file under the
    // accounts root), so the app reopens straight into it with no picker.
    QString readLastActive() const;
    void writeLastActive(const QString& id) const;

    // Accounts the user turned offline are remembered across runs (a file under
    // the accounts root) and are NOT auto-opened at startup, so a disabled
    // account stays offline. Loaded once at construction; persisted on toggle.
    void loadOfflineSet();
    void persistOfflineSet() const;
    void setAccountOffline(const QString& id, bool offline);

    // Global settings (a small JSON file under the accounts root), loaded once at
    // construction and persisted on change. Currently just full privacy mode.
    void loadSettings();
    void persistSettings() const;

    std::unique_ptr<client::AccountManager> manager_;
    AccountListModel accountList_;
    // The accounts found on disk at the last refresh. The accounts list is
    // rebuilt on every unread count change and must not reopen an account
    // database to do it - each open runs the key derivation.
    QVector<AccountListRow> accountRows_;
    // What a popup says under its title: what happened, and - when more than one
    // account is open - which of them it happened to.
    QString notificationBody(const SessionController* ctrl, const QString& what) const;

    OpenAccountsModel accounts_;
    QVector<AccountRow> accountStatuses_;
    bool notifications_ = true;
    // The call state each account was last seen in, so a call is announced when it
    // starts ringing and not again on every tick that follows.
    QHash<QString, QString> callStates_;
    QList<SessionController*> sessions_;  // open accounts, owned (parented here)
    QString activeId_;
    bool haveAccounts_ = false;
    // Ids of accounts the user turned offline (persisted; not auto-opened).
    QSet<QString> offline_;
    // The account an unlock prompt is open for, and whether unlocking it was
    // asked for in order to bring it online. An account unlocked just to be read
    // keeps whatever the switch says.
    QString unlockingId_;
    // An account unlocked for the sole purpose of deleting it.
    QString pendingDeleteId_;
    bool unlockToBringOnline_ = false;
    // Global full-privacy mode (persisted; applied process-wide on load/change).
    bool fullPrivacy_ = false;
};

}  // namespace bazarish::app
