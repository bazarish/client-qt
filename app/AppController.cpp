// Bazarish project (c) 2026
#include "AppController.hpp"

#include <QUrl>

#include <cstdlib>
#include <exception>
#include <fstream>

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

std::filesystem::path lastActivePath()
{
    return profilesRoot() / ".active";
}

std::filesystem::path offlinePath()
{
    return profilesRoot() / ".offline";
}
}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
    , manager_(std::make_unique<client::ProfileManager>(profilesRoot()))
{
    refreshProfiles();
    // Accounts the user turned offline last run must stay offline: load that set
    // before opening anything so they are skipped.
    loadOfflineSet();
    // Open every unencrypted profile in the background so they are all online by
    // default (except the ones kept offline), then focus the last active one -
    // no startup dialog when at least one profile could be opened.
    openAllProfiles();
    const QString last = readLastActive();
    if (sessionFor(last) != nullptr) {
        activeId_ = last;
    } else if (!sessions_.isEmpty()) {
        activeId_ = sessions_.first()->accountId();
    }
    if (!activeId_.isEmpty()) {
        writeLastActive(activeId_);  // stabilize the choice across runs
    }
    refreshAccounts();
}

SessionController* AppController::sessionFor(const QString& id) const
{
    for (SessionController* ctrl : sessions_) {
        if (ctrl->accountId() == id) {
            return ctrl;
        }
    }
    return nullptr;
}

SessionController* AppController::activeController() const
{
    return sessionFor(activeId_);
}

QObject* AppController::session()
{
    return activeController();
}

QString AppController::readLastActive() const
{
    std::ifstream in(lastActivePath());
    std::string id;
    std::getline(in, id);
    return QString::fromStdString(id);
}

void AppController::writeLastActive(const QString& id) const
{
    std::ofstream out(lastActivePath(), std::ios::trunc);
    out << id.toStdString();
}

void AppController::loadOfflineSet()
{
    offline_.clear();
    std::ifstream in(offlinePath());
    std::string id;
    while (std::getline(in, id)) {
        if (!id.empty()) {
            offline_.insert(QString::fromStdString(id));
        }
    }
}

void AppController::persistOfflineSet() const
{
    std::ofstream out(offlinePath(), std::ios::trunc);
    for (const QString& id : offline_) {
        out << id.toStdString() << '\n';
    }
}

void AppController::setAccountOffline(const QString& id, bool offline)
{
    const bool changed = offline ? (offline_.constFind(id) == offline_.cend())
                                 : (offline_.constFind(id) != offline_.cend());
    if (!changed) {
        return;
    }
    if (offline) {
        offline_.insert(id);
    } else {
        offline_.remove(id);
    }
    persistOfflineSet();
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

void AppController::refreshAccounts()
{
    // The unified list is every on-disk profile, with live status merged in for
    // the ones currently open.
    QVector<AccountRow> rows;
    std::vector<client::ProfileInfo> infos;
    try {
        infos = manager_->list();
    } catch (const std::exception&) {
        infos.clear();
    }
    for (const client::ProfileInfo& info : infos) {
        const QString id = QString::fromStdString(info.id);
        AccountRow row;
        row.id = id;
        row.encrypted = info.encrypted;
        if (SessionController* ctrl = sessionFor(id)) {
            row.open = true;
            row.active = (id == activeId_);
            row.online = ctrl->online();
            row.connected = ctrl->reachable();
            row.unread = ctrl->unreadTotal();
            // The active connection: which facade, and whether it is an I2P
            // facade (host ends in ".b32.i2p") - drives the account list's
            // positive green marking vs grey for a clearnet facade.
            row.activeFacade = ctrl->activeFacade();
            row.i2pFacade = row.activeFacade.contains(QStringLiteral(".b32.i2p"));
            row.name = ctrl->displayName().isEmpty() ? QString::fromStdString(info.name)
                                                      : ctrl->displayName();
            row.fingerprint = ctrl->fingerprint().isEmpty()
                ? QString::fromStdString(info.fingerprint)
                : ctrl->fingerprint();
        } else {
            row.name = QString::fromStdString(info.name);
            row.fingerprint = QString::fromStdString(info.fingerprint);
        }
        rows.push_back(std::move(row));
    }
    accounts_.setAccounts(std::move(rows));
    emit accountsChanged();
}

void AppController::setActive(const QString& id)
{
    if (id == activeId_) {
        return;
    }
    activeId_ = id;
    writeLastActive(id);
    emit sessionChanged();
    refreshAccounts();
}

void AppController::openSession(const QString& id, const QString& passphrase, bool makeActive)
{
    // Already open: just focus it (or do nothing for a background request).
    if (sessionFor(id) != nullptr) {
        if (makeActive) {
            setActive(id);
            emit profileOpened();
        }
        return;
    }

    // An encrypted profile needs its passphrase; ask the UI for it.
    if (passphrase.isEmpty() && manager_->exists(id.toStdString())) {
        try {
            for (const client::ProfileInfo& info : manager_->list()) {
                if (info.id == id.toStdString() && info.encrypted) {
                    emit needPassphrase(id, QString::fromStdString(info.name));
                    return;
                }
            }
        } catch (const std::exception&) {
            // fall through and attempt the open
        }
    }

    auto* ctrl = new SessionController(this);
    sessions_.append(ctrl);

    connect(ctrl, &SessionController::identityChanged, this, [this, ctrl, makeActive]() {
        if (!ctrl->fingerprint().isEmpty()) {
            refreshAccounts();
            if (makeActive && ctrl->accountId() == activeId_) {
                emit profileOpened();
            }
        }
    });
    connect(ctrl, &SessionController::openFailed, this, [this, ctrl](const QString& error) {
        removeSession(ctrl, /*deferred=*/true);
        emit profileOpenFailed(error);
    });
    connect(ctrl, &SessionController::unreadTotalChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::onlineChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::reachableChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::connectedChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::facadeInfoChanged, this, &AppController::refreshAccounts);

    try {
        const QString dir = QString::fromStdString(manager_->dirFor(id.toStdString()).string());
        ctrl->open(dir, id, passphrase);
    } catch (const std::exception& e) {
        removeSession(ctrl, /*deferred=*/false);
        emit profileOpenFailed(QString::fromUtf8(e.what()));
        return;
    }
    if (makeActive) {
        setActive(id);
    }
    refreshAccounts();
}

void AppController::openAllProfiles()
{
    std::vector<client::ProfileInfo> infos;
    try {
        infos = manager_->list();
    } catch (const std::exception&) {
        return;
    }
    for (const client::ProfileInfo& info : infos) {
        const QString id = QString::fromStdString(info.id);
        // Skip accounts the user turned offline: they stay closed (shown as
        // Offline) until explicitly switched on, so the choice survives a restart.
        if (!info.encrypted && offline_.constFind(id) == offline_.cend()) {
            openSession(id, {}, /*makeActive=*/false);
        }
    }
}

void AppController::removeSession(SessionController* ctrl, bool deferred)
{
    const QString id = ctrl->accountId();
    sessions_.removeAll(ctrl);
    if (deferred) {
        ctrl->deleteLater();
    } else {
        delete ctrl;  // destructor joins the worker thread and closes the transcript
    }
    if (activeId_ == id) {
        activeId_ = sessions_.isEmpty() ? QString() : sessions_.first()->accountId();
        writeLastActive(activeId_);
        emit sessionChanged();
    }
    refreshAccounts();
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
    openSession(QString::fromStdString(id), passphrase, /*makeActive=*/true);
}

void AppController::openProfile(const QString& id, const QString& passphrase)
{
    // Explicitly opening an account brings it online; clear any persisted offline
    // mark so it auto-opens on the next run too.
    setAccountOffline(id, false);
    openSession(id, passphrase, /*makeActive=*/true);
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
    openSession(QString::fromStdString(id), atRestPassphrase, /*makeActive=*/true);
}

void AppController::deleteProfile(const QString& id)
{
    // If the account is open, tear its session down first (synchronously, so the
    // transcript is flushed and closed) before removing the directory.
    if (SessionController* ctrl = sessionFor(id)) {
        removeSession(ctrl, /*deferred=*/false);
    }
    try {
        manager_->remove(id.toStdString());
    } catch (const std::exception&) {
        // best effort
    }
    refreshProfiles();
    refreshAccounts();
}

void AppController::switchTo(const QString& id)
{
    if (sessionFor(id) != nullptr) {
        setActive(id);
    } else {
        // Switching to a closed account opens it = brings it online.
        setAccountOffline(id, false);
        openSession(id, {}, /*makeActive=*/true);
    }
}

void AppController::setOnline(const QString& id, bool on)
{
    // Remember the choice across runs: an offline account is not auto-opened next
    // launch; an online one is.
    setAccountOffline(id, !on);
    SessionController* ctrl = sessionFor(id);
    if (on) {
        if (ctrl != nullptr) {
            ctrl->goOnline();
            refreshAccounts();
        } else {
            openSession(id, {}, /*makeActive=*/false);
        }
    } else if (ctrl != nullptr) {
        ctrl->goOffline();
        refreshAccounts();
    }
}

void AppController::requestAddAccount()
{
    emit showPicker();
}

void AppController::closeProfile()
{
    if (SessionController* ctrl = activeController()) {
        removeSession(ctrl, /*deferred=*/false);
    }
    refreshProfiles();
}

}  // namespace bazarish::app
