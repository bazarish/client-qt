// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "ProfileManager.hpp"
#include "SessionController.hpp"

#include <QObject>
#include <QString>

#include <memory>

namespace bazarish::app {

// Root application object: owns the profile manager and the currently open
// session. Drives the no-arguments launch flow (pick or create a profile,
// then unlock it).
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* profiles READ profiles CONSTANT)
    Q_PROPERTY(QObject* session READ session NOTIFY sessionChanged)
    Q_PROPERTY(bool hasProfiles READ hasProfiles NOTIFY profilesChanged)
public:
    explicit AppController(QObject* parent = nullptr);

    QObject* profiles() { return &profiles_; }
    QObject* session() { return session_.get(); }
    bool hasProfiles() const { return haveProfiles_; }

    Q_INVOKABLE void refreshProfiles();
    Q_INVOKABLE void createProfile(const QString& name, const QString& passphrase);
    Q_INVOKABLE void openProfile(const QString& id, const QString& passphrase);
    Q_INVOKABLE void importProfile(const QString& name, const QString& fileUrl,
        const QString& password, const QString& atRestPassphrase);
    Q_INVOKABLE void deleteProfile(const QString& id);
    Q_INVOKABLE void closeProfile();

signals:
    void profilesChanged();
    void sessionChanged();
    void profileOpened();
    void profileOpenFailed(const QString& error);
    void createFailed(const QString& error);

private:
    void startSessionFor(const QString& id, const QString& passphrase);

    std::unique_ptr<client::ProfileManager> manager_;
    ProfileListModel profiles_;
    std::unique_ptr<SessionController> session_;
    bool haveProfiles_ = false;
};

}  // namespace bazarish::app
