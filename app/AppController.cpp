// Bazarish project (c) 2026
#include "AppController.hpp"

#include <QUrl>

#include <cstdlib>
#include <exception>

namespace bazarish::app {

namespace {
std::filesystem::path profilesRoot()
{
    if (const char* const env = std::getenv("BAZARISH_PROFILES_DIR");
        env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return client::ProfileManager::defaultRoot();
}
}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
    , manager_(std::make_unique<client::ProfileManager>(profilesRoot()))
{
    refreshProfiles();
}

void AppController::refreshProfiles()
{
    QVector<ProfileRow> rows;
    try {
        for (const client::ProfileInfo& info : manager_->list()) {
            rows.push_back(ProfileRow{QString::fromStdString(info.id),
                QString::fromStdString(info.name), QString::fromStdString(info.fingerprint),
                info.encrypted, info.connected});
        }
    } catch (const std::exception&) {
        // A malformed profile dir should not break the picker.
    }
    haveProfiles_ = !rows.isEmpty();
    profiles_.setProfiles(std::move(rows));
    emit profilesChanged();
}

void AppController::startSessionFor(const QString& id, const QString& passphrase)
{
    session_ = std::make_unique<SessionController>();
    connect(session_.get(), &SessionController::identityChanged, this, [this]() {
        if (!session_->fingerprint().isEmpty()) {
            emit profileOpened();
        }
    });
    connect(session_.get(), &SessionController::openFailed, this,
        &AppController::profileOpenFailed);
    emit sessionChanged();
    const QString dir = QString::fromStdString(manager_->dirFor(id.toStdString()).string());
    session_->open(dir, id, passphrase);
}

void AppController::createProfile(const QString& name, const QString& passphrase)
{
    std::string id;
    try {
        const client::ProfileInfo info
            = manager_->create(name.toStdString(), passphrase.toStdString());
        id = info.id;
    } catch (const std::exception& e) {
        emit createFailed(QString::fromUtf8(e.what()));
        return;
    }
    refreshProfiles();
    startSessionFor(QString::fromStdString(id), passphrase);
}

void AppController::openProfile(const QString& id, const QString& passphrase)
{
    startSessionFor(id, passphrase);
}

void AppController::importProfile(const QString& name, const QString& fileUrl,
    const QString& password, const QString& atRestPassphrase)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    std::string id;
    try {
        const client::ProfileInfo info = manager_->import(name.toStdString(),
            localPath.toStdString(), password.toStdString(), atRestPassphrase.toStdString());
        id = info.id;
    } catch (const std::exception& e) {
        emit createFailed(QString::fromUtf8(e.what()));
        return;
    }
    refreshProfiles();
    startSessionFor(QString::fromStdString(id), atRestPassphrase);
}

void AppController::deleteProfile(const QString& id)
{
    try {
        manager_->remove(id.toStdString());
    } catch (const std::exception&) {
        // best effort
    }
    refreshProfiles();
}

void AppController::closeProfile()
{
    session_.reset();
    emit sessionChanged();
    refreshProfiles();
}

}  // namespace bazarish::app
