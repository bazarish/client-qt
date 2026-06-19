// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "ProfileManager.hpp"
#include "SessionController.hpp"

#include <QList>
#include <QObject>
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
public:
    explicit AppController(QObject* parent = nullptr);

    QObject* profiles() { return &profiles_; }
    QObject* accounts() { return &accounts_; }
    QObject* session();
    bool hasProfiles() const { return haveProfiles_; }
    bool hasOpenAccounts() const { return !sessions_.isEmpty(); }

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

    std::unique_ptr<client::ProfileManager> manager_;
    ProfileListModel profiles_;
    OpenAccountsModel accounts_;
    QList<SessionController*> sessions_;  // open accounts, owned (parented here)
    QString activeId_;
    bool haveProfiles_ = false;
};

}  // namespace bazarish::app
