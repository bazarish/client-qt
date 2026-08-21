// Bazarish project (c) 2026
#include "AppController.hpp"

#include "I2pRouter.hpp"

#include <bazarish/I2p.hpp>

// Qt makes `emit` a macro and the log header declares a function of that name,
// so the keyword is stood down for the length of this include.
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QDir>
#include <QStandardPaths>
#include <QUrl>

#include <nlohmann/json.hpp>

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

std::filesystem::path settingsPath()
{
    return profilesRoot() / ".settings";
}
}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
    , manager_(std::make_unique<client::ProfileManager>(profilesRoot()))
{
    refreshProfiles();
    // Apply global settings (e.g. full privacy mode) before opening any profile, so
    // the first background sync already honours them.
    loadSettings();
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

void AppController::loadSettings()
{
    fullPrivacy_ = false;
    try {
        std::ifstream in(settingsPath());
        if (in.good()) {
            nlohmann::json j;
            in >> j;
            fullPrivacy_ = j.value("fullPrivacyMode", false);
        }
    } catch (const std::exception& error) {
        // A missing or malformed settings file just means defaults - but silently
        // reverting privacy mode to off is exactly what must not go unsaid.
        bazarish::log::warn("settings not read, using defaults: {}", error.what());
    }
    client::setFullPrivacy(fullPrivacy_);
}

void AppController::persistSettings() const
{
    const nlohmann::json j = {{"fullPrivacyMode", fullPrivacy_}};
    std::ofstream out(settingsPath(), std::ios::trunc);
    out << j.dump();
}

void AppController::setFullPrivacyMode(bool on)
{
    if (fullPrivacy_ == on) {
        return;
    }
    fullPrivacy_ = on;
    client::setFullPrivacy(on);  // takes effect on the next request, process-wide
    persistSettings();
    emit fullPrivacyModeChanged();
}

QString AppController::i2pdVersion() const
{
    return QString::fromStdString(bazarish::i2p::routerVersion());
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
    } catch (const std::exception& error) {
        // A malformed profile dir should not break the picker.
        bazarish::log::warn("profile list incomplete: {}", error.what());
    }
    haveProfiles_ = !rows.isEmpty();
    profileRows_ = rows;
    profiles_.setProfiles(std::move(rows));
    emit profilesChanged();
}

void AppController::refreshProfileRows()
{
    // The picker lists what is on disk, but an open profile knows better: it has
    // just connected to a server, or learnt its own name, while the listing was
    // taken before any of that. Patch the rows from the live sessions instead of
    // re-reading the files - unlocking a profile database is expensive by design.
    bool changed = false;
    for (ProfileRow& row : profileRows_) {
        const SessionController* const ctrl = sessionFor(row.id);
        if (ctrl == nullptr) {
            continue;
        }
        const bool connected = ctrl->connected();
        const QString fingerprint = ctrl->fingerprint();
        const QString name = ctrl->displayName();
        if (row.connected != connected || (!fingerprint.isEmpty() && row.fingerprint != fingerprint)
            || (!name.isEmpty() && row.name != name)) {
            row.connected = connected;
            if (!fingerprint.isEmpty()) {
                row.fingerprint = fingerprint;
            }
            if (!name.isEmpty()) {
                row.name = name;
            }
            changed = true;
        }
    }
    if (changed) {
        profiles_.setProfiles(profileRows_);
        emit profilesChanged();
    }
}

void AppController::refreshAccounts()
{
    refreshProfileRows();
    // The unified list is every on-disk profile, with live status merged in for
    // the ones currently open. It reads the profiles this controller already
    // listed, never the disk: this runs on every unread count change, and opening
    // a profile database means running its key derivation (a quarter of a second
    // each, by design).
    QVector<AccountRow> rows;
    for (const ProfileRow& info : profileRows_) {
        const QString id = info.id;
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
            row.name = ctrl->displayName().isEmpty() ? info.name : ctrl->displayName();
            row.fingerprint
                = ctrl->fingerprint().isEmpty() ? info.fingerprint : ctrl->fingerprint();
        } else {
            row.name = info.name;
            row.fingerprint = info.fingerprint;
        }
        rows.push_back(std::move(row));
    }
    accounts_.setAccounts(std::move(rows));
    // Warm spares are only ever handed to a lookup an open account makes. With
    // every account offline nobody will ask, so the pool stops holding tunnels
    // open on their behalf.
    bool anyOnline = false;
    for (const SessionController* const session : sessions_) {
        if (session != nullptr && session->online()) {
            anyOnline = true;
            break;
        }
    }
    client::setWarmDestsWanted(anyOnline);
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

int AppController::unreadElsewhere() const
{
    int total = 0;
    for (const SessionController* const session : sessions_) {
        if (session != nullptr && session->accountId() != activeId_) {
            total += session->unreadTotal();
        }
    }
    return total;
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
            for (const ProfileRow& info : profileRows_) {
                if (info.id == id && info.encrypted) {
                    emit needPassphrase(id, info.name);
                    return;
                }
            }
        } catch (const std::exception& error) {
            // fall through and attempt the open
            bazarish::log::warn("could not tell whether the profile is encrypted: {}",
                error.what());
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
        const QString file
            = QString::fromStdString(manager_->fileFor(id.toStdString()).string());
        ctrl->open(file, id, passphrase);
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
    for (const ProfileRow& info : profileRows_) {
        const QString id = info.id;
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
    } catch (const std::exception& error) {
        // What is left behind is on disk, and the user must be able to find out.
        bazarish::log::warn("profile directory not removed: {}", error.what());
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

bool AppController::portable() const
{
    return client::ProfileManager::portable();
}

QString AppController::dataLocation() const
{
    return QString::fromStdString(client::ProfileManager::dataRoot().string());
}

void AppController::setPortable(const bool on)
{
    namespace fs = std::filesystem;
    if (on == portable()) {
        return;
    }
    const fs::path from = on ? client::ProfileManager::globalRoot()
                             : client::ProfileManager::portableRoot();
    const fs::path to = on ? client::ProfileManager::portableRoot()
                           : client::ProfileManager::globalRoot();
    // Close everything first: profiles hold their transcripts open, and moving a
    // directory out from under them would be moving files that are being written.
    closeAllSessions();
    std::error_code error;
    if (fs::exists(from, error) && !fs::is_empty(from, error)) {
        if (fs::exists(to, error) && !fs::is_empty(to, error)) {
            emit createFailed(QStringLiteral("There is already data at ")
                + QString::fromStdString(to.string()) + QStringLiteral(" - move or remove it "
                    "first, so nothing is overwritten."));
            return;
        }
        fs::create_directories(to.parent_path(), error);
        fs::rename(from, to, error);
        if (error) {
            // Across devices rename fails; copy then remove, which is the same
            // thing at a cost.
            error.clear();
            fs::copy(from, to, fs::copy_options::recursive, error);
            if (error) {
                emit createFailed(QStringLiteral("Could not move the data: ")
                    + QString::fromStdString(error.message()));
                return;
            }
            fs::remove_all(from, error);
        }
    }
    if (on) {
        std::ofstream marker(client::ProfileManager::portableMarker(), std::ios::trunc);
        marker << "bazarish keeps its data in bazarish_data beside this file\n";
    } else {
        fs::remove(client::ProfileManager::portableMarker(), error);
    }
    emit portableChanged();
    emit restartRequired(on
            ? QStringLiteral("Your data now lives beside the app. Start Bazarish again to use it.")
            : QStringLiteral("Your data moved back to your user folder. Start Bazarish again to "
                             "use it."));
}

QString AppController::scratchFile(const QString& name) const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name);
}

void AppController::rebuildI2pLinks()
{
    client::flushWarmDests();
    for (SessionController* const session : sessions_) {
        if (session != nullptr) {
            session->rebuildI2pLinks();
        }
    }
}

void AppController::requestAddAccount()
{
    emit showPicker();
}

void AppController::closeAllSessions()
{
    const QVector<SessionController*> open = sessions_;
    for (SessionController* const ctrl : open) {
        removeSession(ctrl, /*deferred=*/false);
    }
    refreshAccounts();
}

void AppController::closeProfile()
{
    if (SessionController* ctrl = activeController()) {
        removeSession(ctrl, /*deferred=*/false);
    }
    refreshProfiles();
}

}  // namespace bazarish::app
