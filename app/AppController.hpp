// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "ProfileManager.hpp"
#include "SessionController.hpp"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

namespace bazarish::app {

// Root application object: owns the profile manager and the set of currently
// open accounts. Several accounts can be open at once - each keeps its own
// SessionController (worker thread + background sync), so all of them receive -
// and one is "active" (the one the UI is bound to). Drives the launch flow
// (pick or create a profile, then unlock it) and account switching/removal.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* profiles READ profiles CONSTANT)
    Q_PROPERTY(QObject* accounts READ accounts CONSTANT)
    Q_PROPERTY(QObject* session READ session NOTIFY sessionChanged)
    Q_PROPERTY(bool hasProfiles READ hasProfiles NOTIFY profilesChanged)
    Q_PROPERTY(bool hasOpenAccounts READ hasOpenAccounts NOTIFY accountsChanged)
    // Unread waiting in accounts other than the one on screen: the switcher is
    // the only place they would ever be noticed.
    Q_PROPERTY(int unreadElsewhere READ unreadElsewhere NOTIFY accountsChanged)
    // --- Global (app-wide) settings, shared by every profile ---
    // Full privacy mode: forbid connecting through any clearnet client-facade, so
    // all traffic runs over I2P only. Persisted across runs and applied process-wide.
    Q_PROPERTY(bool fullPrivacyMode READ fullPrivacyMode WRITE setFullPrivacyMode
            NOTIFY fullPrivacyModeChanged)
    // The embedded upstream i2pd engine version (e.g. "2.60.0"), for display.
    Q_PROPERTY(QString i2pdVersion READ i2pdVersion CONSTANT)
public:
    explicit AppController(QObject* parent = nullptr);

    QObject* profiles() { return &profiles_; }
    QObject* accounts() { return &accounts_; }
    int unreadElsewhere() const;
    QObject* session();
    bool hasProfiles() const { return haveProfiles_; }
    bool hasOpenAccounts() const { return !sessions_.isEmpty(); }
    bool fullPrivacyMode() const { return fullPrivacy_; }
    void setFullPrivacyMode(bool on);
    QString i2pdVersion() const;

    Q_INVOKABLE void refreshProfiles();
    Q_INVOKABLE void createProfile(const QString& name, const QString& passphrase);
    Q_INVOKABLE void openProfile(const QString& id, const QString& passphrase);
    Q_INVOKABLE void importProfile(const QString& name, const QString& fileUrl,
        const QString& password, const QString& atRestPassphrase);
    Q_INVOKABLE void deleteProfile(const QString& id);
    // Makes an account the active (focused) one, opening it first if needed.
    // An encrypted, not-yet-open account emits needPassphrase instead.
    Q_INVOKABLE void switchTo(const QString& id);
    // Brings a specific account online (open + sync) or offline (stop syncing),
    // independently of which account is active. Bringing an encrypted, unopened
    // account online emits needPassphrase.
    Q_INVOKABLE void setOnline(const QString& id, bool on);
    // Asks the UI to show the picker so another account can be added, without
    // closing the open ones.
    Q_INVOKABLE void requestAddAccount();
    // Signs out (closes) the active account; switches to another if any remain.
    Q_INVOKABLE void closeProfile();

signals:
    void profilesChanged();
    void accountsChanged();
    void sessionChanged();
    void fullPrivacyModeChanged();
    void profileOpened();
    void profileOpenFailed(const QString& error);
    void createFailed(const QString& error);
    void showPicker();
    // An encrypted account needs its passphrase before it can be opened.
    void needPassphrase(const QString& id, const QString& name);

private:
    SessionController* sessionFor(const QString& id) const;
    SessionController* activeController() const;
    // Opens a profile into a SessionController. makeActive focuses it (and
    // routes the UI to it); pass it false to open in the background. Encrypted
    // profiles opened with an empty passphrase are reported via needPassphrase.
    void openSession(const QString& id, const QString& passphrase, bool makeActive);
    // Opens every unencrypted profile in the background at startup.
    void openAllProfiles();
    // Removes and tears down an open account's controller. deferred uses
    // deleteLater (required when called from within the controller's own
    // signal); otherwise the controller is destroyed synchronously, so its
    // transcript is flushed and closed before any on-disk removal.
    void removeSession(SessionController* ctrl, bool deferred);
    void refreshAccounts();
    void setActive(const QString& id);
    // The last active account is remembered across runs (a file under the
    // profiles root), so the app reopens straight into it with no picker.
    QString readLastActive() const;
    void writeLastActive(const QString& id) const;

    // Accounts the user turned offline are remembered across runs (a file under
    // the profiles root) and are NOT auto-opened at startup, so a disabled
    // account stays offline. Loaded once at construction; persisted on toggle.
    void loadOfflineSet();
    void persistOfflineSet() const;
    void setAccountOffline(const QString& id, bool offline);

    // Global settings (a small JSON file under the profiles root), loaded once at
    // construction and persisted on change. Currently just full privacy mode.
    void loadSettings();
    void persistSettings() const;

    std::unique_ptr<client::ProfileManager> manager_;
    ProfileListModel profiles_;
    OpenAccountsModel accounts_;
    QList<SessionController*> sessions_;  // open accounts, owned (parented here)
    QString activeId_;
    bool haveProfiles_ = false;
    // Ids of accounts the user turned offline (persisted; not auto-opened).
    QSet<QString> offline_;
    // Global full-privacy mode (persisted; applied process-wide on load/change).
    bool fullPrivacy_ = false;
};

}  // namespace bazarish::app
