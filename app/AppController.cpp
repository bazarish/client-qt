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

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <exception>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace bazarish::app {

namespace {
std::filesystem::path accountsRoot()
{
    if (const char* const env = std::getenv("BAZARISH_ACCOUNTS_DIR");
        env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return client::AccountManager::defaultRoot();
}

std::filesystem::path lastActivePath()
{
    return accountsRoot() / ".active";
}

std::filesystem::path offlinePath()
{
    return accountsRoot() / ".offline";
}

std::filesystem::path settingsPath()
{
    return accountsRoot() / ".settings";
}
}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
    , manager_(std::make_unique<client::AccountManager>(accountsRoot()))
{
    refreshAccountList();
    // Apply global settings (e.g. full privacy mode) before opening any account, so
    // the first background sync already honours them.
    loadSettings();
    // Accounts the user turned offline last run must stay offline: load that set
    // before opening anything so they are skipped.
    loadOfflineSet();
    // Open every unencrypted account in the background so they are all online by
    // default (except the ones kept offline), then focus the last active one -
    // no startup dialog when at least one account could be opened.
    openAllAccounts();
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
    notifications_ = true;
    try {
        std::ifstream in(settingsPath());
        if (in.good()) {
            nlohmann::json j;
            in >> j;
            fullPrivacy_ = j.value("fullPrivacyMode", false);
            notifications_ = j.value("notifications", true);
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
    const nlohmann::json j
        = {{"fullPrivacyMode", fullPrivacy_}, {"notifications", notifications_}};
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

QString AppController::notificationBody(
    const SessionController* const ctrl, const QString& what) const
{
    if (sessions_.size() < 2 || ctrl == nullptr || ctrl->displayName().isEmpty()) {
        return what;
    }
    return what + QStringLiteral(" - ") + ctrl->displayName();
}

QString AppController::soundFolder() const
{
    return QString::fromStdString(accountsRoot().string());
}

void AppController::setNotificationsEnabled(const bool on)
{
    if (notifications_ == on) {
        return;
    }
    notifications_ = on;
    persistSettings();
    emit notificationsEnabledChanged();
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

void AppController::refreshAccountList()
{
    QVector<AccountListRow> rows;
    try {
        for (const client::AccountInfo& info : manager_->list()) {
            rows.push_back(AccountListRow{QString::fromStdString(info.id),
                QString::fromStdString(info.name), QString::fromStdString(info.fingerprint),
                info.encrypted, info.connected});
        }
    } catch (const std::exception& error) {
        // A malformed account dir should not break the picker.
        bazarish::log::warn("account list incomplete: {}", error.what());
    }
    haveAccounts_ = !rows.isEmpty();
    accountRows_ = rows;
    accountList_.setAccounts(std::move(rows));
    emit accountListChanged();
}

void AppController::refreshAccountRows()
{
    // The picker lists what is on disk, but an open account knows better: it has
    // just connected to a server, or learnt its own name, while the listing was
    // taken before any of that. Patch the rows from the live sessions instead of
    // re-reading the files - unlocking an account database is expensive by design.
    bool changed = false;
    for (AccountListRow& row : accountRows_) {
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
        accountList_.setAccounts(accountRows_);
        emit accountListChanged();
    }
}

void AppController::publishReseedFacades()
{
    // Every clearnet facade of every account that is open. An account that is
    // locked keeps its endpoint inside its encrypted database, so it cannot
    // contribute one until it is unlocked - that is a fact of the storage, not a
    // choice.
    std::vector<std::string> urls;
    for (SessionController* const ctrl : sessions_) {
        if (ctrl == nullptr) {
            continue;
        }
        for (const QString& url : ctrl->configuredFacades()) {
            const std::string text = url.trimmed().toStdString();
            if (!text.empty() && std::find(urls.begin(), urls.end(), text) == urls.end()) {
                urls.push_back(text);
            }
        }
    }
    bazarish::client::setReseedFacades(std::move(urls));
}

void AppController::refreshAccounts()
{
    publishReseedFacades();
    refreshAccountRows();
    // The unified list is every on-disk account, with live status merged in for
    // the ones currently open. It reads the accounts this controller already
    // listed, never the disk: this runs on every unread count change, and opening
    // an account database means running its key derivation (a quarter of a second
    // each, by design).
    QVector<AccountRow> rows;
    for (const AccountListRow& info : accountRows_) {
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
    accountStatuses_ = rows;
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
            emit accountOpened();
        }
        return;
    }

    // An encrypted account needs its passphrase; ask the UI for it.
    if (passphrase.isEmpty() && manager_->exists(id.toStdString())) {
        try {
            for (const AccountListRow& info : accountRows_) {
                if (info.id == id && info.encrypted) {
                    unlockingId_ = id;
                    emit needPassphrase(id, info.name);
                    return;
                }
            }
        } catch (const std::exception& error) {
            // fall through and attempt the open
            bazarish::log::warn("could not tell whether the account is encrypted: {}",
                error.what());
        }
    }

    auto* ctrl = new SessionController(this);
    sessions_.append(ctrl);

    connect(ctrl, &SessionController::identityChanged, this, [this, ctrl, makeActive]() {
        if (!ctrl->fingerprint().isEmpty()) {
            refreshAccounts();
            if (makeActive && ctrl->accountId() == activeId_) {
                emit accountOpened();
            }
        }
    });
    connect(ctrl, &SessionController::openFailed, this, [this, ctrl, id](const QString& error) {
        removeSession(ctrl, /*deferred=*/true);
        if (unlockingId_ == id) {
            // The prompt stays open with the reason on it; the account it was
            // opened for does not come online on a wrong passphrase.
            refreshAccounts();
            emit unlockFailed(error);
            return;
        }
        emit accountOpenFailed(error);
    });
    connect(ctrl, &SessionController::messageNotification, this,
        [this, ctrl](const QString& fromName) {
            emit notificationRequested(fromName, notificationBody(ctrl, tr("New message")));
        });
    // A call is announced when it starts ringing: the state is republished on
    // every sync tick, and only the change into "incoming" is news.
    connect(ctrl, &SessionController::callChanged, this, [this, ctrl]() {
        const QString state = ctrl->callState();
        const QString previous = callStates_.value(ctrl->accountId());
        callStates_.insert(ctrl->accountId(), state);
        if (state == QLatin1String("incoming") && previous != state) {
            emit notificationRequested(
                ctrl->callPeerName(), notificationBody(ctrl, tr("Incoming call")));
        }
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
        if (unlockingId_ == id) {
            refreshAccounts();
            emit unlockFailed(QString::fromUtf8(e.what()));
            return;
        }
        emit accountOpenFailed(QString::fromUtf8(e.what()));
        return;
    }
    if (makeActive) {
        setActive(id);
    }
    refreshAccounts();
}

void AppController::openAllAccounts()
{
    for (const AccountListRow& info : accountRows_) {
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

void AppController::createAccount(const QString& name, const QString& passphrase)
{
    std::string id;
    try {
        const client::AccountInfo info
            = manager_->create(name.toStdString(), passphrase.toStdString());
        id = info.id;
    } catch (const std::exception& e) {
        emit createFailed(QString::fromUtf8(e.what()));
        return;
    }
    refreshAccountList();
    openSession(QString::fromStdString(id), passphrase, /*makeActive=*/true);
}

void AppController::openAccount(const QString& id, const QString& passphrase)
{
    // Unlocking is not the same as switching on. An account the user turned off
    // is unlocked to be read: it opens, its chats are there, and it stays off
    // until the switch says otherwise. Only an unlock asked for by that switch -
    // or an account that was never turned off - comes online here.
    const bool wasOff = offline_.constFind(id) != offline_.cend();
    const bool bringOnline = !wasOff || (unlockingId_ == id && unlockToBringOnline_);
    // The attempt is still an unlock until it is known to have worked: clearing
    // this before the open meant a wrong passphrase was reported as if nobody had
    // been asked for one, which is to say not reported at all.
    unlockingId_ = id;
    openSession(id, passphrase, /*makeActive=*/true);
    if (sessionFor(id) == nullptr) {
        return;  // it did not open; the prompt has been told why
    }
    unlockingId_.clear();
    unlockToBringOnline_ = false;
    emit accountUnlocked(id);
    if (bringOnline) {
        setAccountOffline(id, false);
    }
    if (!bringOnline) {
        if (SessionController* const ctrl = sessionFor(id)) {
            ctrl->goOffline();
        }
        refreshAccounts();
    }
}

void AppController::cancelUnlock()
{
    unlockingId_.clear();
    unlockToBringOnline_ = false;
    // Nothing changed on disk while the prompt was open, so redrawing the rows
    // puts every switch back to what it says there.
    refreshAccounts();
}

void AppController::importAccount(const QString& name, const QString& fileUrl,
    const QString& password, const QString& atRestPassphrase)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    std::string id;
    try {
        const client::AccountInfo info = manager_->import(name.toStdString(),
            localPath.toStdString(), password.toStdString(), atRestPassphrase.toStdString());
        id = info.id;
    } catch (const std::exception& e) {
        emit createFailed(QString::fromUtf8(e.what()));
        return;
    }
    refreshAccountList();
    openSession(QString::fromStdString(id), atRestPassphrase, /*makeActive=*/true);
}

void AppController::deleteAccount(const QString& id)
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
        bazarish::log::warn("account directory not removed: {}", error.what());
    }
    refreshAccountList();
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
    SessionController* ctrl = sessionFor(id);
    if (!on) {
        // Remembered across runs: an account switched off is not opened at the
        // next launch either.
        setAccountOffline(id, true);
        if (ctrl != nullptr) {
            ctrl->goOffline();
        }
        refreshAccounts();
        return;
    }
    if (ctrl != nullptr) {
        setAccountOffline(id, false);
        ctrl->goOnline();
        refreshAccounts();
        return;
    }
    // A locked account cannot come online until it is unlocked, and the switch
    // must not claim otherwise in the meantime: nothing is written here, and
    // openAccount writes it once the passphrase actually opens the account.
    unlockToBringOnline_ = true;
    openSession(id, {}, /*makeActive=*/false);
    if (unlockingId_.isEmpty()) {
        // Not a locked account: it opened (or failed) on the spot.
        unlockToBringOnline_ = false;
        setAccountOffline(id, false);
        refreshAccounts();
    }
}

bool AppController::portable() const
{
    return client::AccountManager::portable();
}

QString AppController::dataLocation() const
{
    return QString::fromStdString(client::AccountManager::dataRoot().string());
}

namespace {

// Whether a path sits inside the system's temporary directory. An AppImage run
// with APPIMAGE_EXTRACT_AND_RUN unpacks itself there, and the unpacked copy is
// writable - so "beside the application" would be a directory that the next
// reboot clears, with every account in it.
bool underTempDirectory(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path temp
        = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(), error);
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return false;
    }
    const auto mismatch = std::mismatch(temp.begin(), temp.end(), candidate.begin(),
        candidate.end());
    return mismatch.first == temp.end();
}

// Whether a directory can be created in and written to, answered by doing it:
// permissions alone do not say whether the filesystem underneath is read-only.
bool directoryIsWritable(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path probe = directory / ".bazarish-write-test";
    std::ofstream out(probe);
    const bool ok = out.is_open() && (out << "x").good();
    out.close();
    std::filesystem::remove(probe, error);
    return ok;
}

}  // namespace

void AppController::setPortable(const bool on)
{
    namespace fs = std::filesystem;
    if (on == portable()) {
        return;
    }
    const fs::path from = on ? client::AccountManager::globalRoot()
                             : client::AccountManager::portableRoot();
    const fs::path to = on ? client::AccountManager::portableRoot()
                           : client::AccountManager::globalRoot();
    // Asked before anything is closed or moved: a directory that cannot be
    // written to (an AppImage on a read-only medium, an application directory
    // owned by root) would otherwise be discovered halfway through, with the
    // accounts already shut and the data already moved.
    if (on && underTempDirectory(client::AccountManager::portableRoot())) {
        emit createFailed(QStringLiteral("This copy of the application is running from a "
                                         "temporary directory (")
            + QString::fromStdString(
                client::AccountManager::portableRoot().parent_path().string())
            + QStringLiteral("), which is cleared on reboot. Keep the application somewhere "
                             "of its own first - a USB stick or a folder - and turn this on "
                             "there."));
        return;
    }
    if (on && !directoryIsWritable(client::AccountManager::portableRoot().parent_path())) {
        emit createFailed(QStringLiteral("Cannot write beside the application (")
            + QString::fromStdString(
                client::AccountManager::portableRoot().parent_path().string())
            + QStringLiteral(") - move it somewhere writable, such as your home directory, "
                             "and try again."));
        return;
    }
    // Close everything first: accounts hold their transcripts open, and moving a
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
        std::ofstream marker(client::AccountManager::portableMarker(), std::ios::trunc);
        marker << "bazarish keeps its data in bazarish_data beside this file\n";
    } else {
        fs::remove(client::AccountManager::portableMarker(), error);
    }
    emit portableChanged();
    // Nothing in this window works from here on: the accounts are closed and the
    // embedded router is still holding the directory that just moved. The dialog
    // this raises has one button, and it quits.
    emit restartRequired(on
            ? QStringLiteral("Your data now lives beside the app. Bazarish has to be started "
                             "again to use it.")
            : QStringLiteral("Your data moved back to your user folder. Bazarish has to be "
                             "started again to use it."));
}

namespace {

// What a picture is allowed to grow to before it is sent. A tunnel carries this
// in a few seconds; a phone camera's original would sit in the transfer for
// minutes and be resized on arrival anyway.
constexpr int kMaxImageEdge = 1600;
// A picture travels inside the message, so what has to fit is not the picture
// but its base64 (a third larger) plus the envelope around it, inside the
// protocol's 512 KiB message ceiling. A quarter of a megabyte leaves room for
// both and still crosses a tunnel in seconds.
constexpr qint64 kMaxImageBytes = 256 * 1024;
constexpr int kJpegQuality = 85;
// Each step down when the encoded picture still does not fit.
constexpr int kQualityStep = 10;
constexpr int kMinJpegQuality = 45;
constexpr double kEdgeStep = 0.75;
constexpr int kMinImageEdge = 640;

// The first bytes of the formats worth rendering. A name says nothing and the
// sender's declared type says less.
bool looksLikePng(const QByteArray& head)
{
    static const QByteArray kSignature
        = QByteArray::fromHex("89504E470D0A1A0A");
    return head.startsWith(kSignature);
}

bool looksLikeJpeg(const QByteArray& head)
{
    return head.size() >= 3 && static_cast<unsigned char>(head[0]) == 0xFF
        && static_cast<unsigned char>(head[1]) == 0xD8 && static_cast<unsigned char>(head[2]) == 0xFF;
}

// Encodes into the smallest of the formats that keeps the picture honest: PNG
// when it has transparency to lose, JPEG otherwise, stepping quality and then
// size down until it fits.
QByteArray encodedImage(QImage image, QString* format)
{
    if (image.width() > kMaxImageEdge || image.height() > kMaxImageEdge) {
        image = image.scaled(kMaxImageEdge, kMaxImageEdge, Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    const bool transparent = image.hasAlphaChannel();
    *format = transparent ? QStringLiteral("png") : QStringLiteral("jpg");
    int quality = kJpegQuality;
    for (;;) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, transparent ? "PNG" : "JPEG", transparent ? -1 : quality)) {
            return {};
        }
        if (bytes.size() <= kMaxImageBytes) {
            return bytes;
        }
        // Quality first (invisible at these sizes), then the picture itself.
        if (!transparent && quality > kMinJpegQuality) {
            quality -= kQualityStep;
            continue;
        }
        const int edge = static_cast<int>(std::max(image.width(), image.height()) * kEdgeStep);
        if (edge < kMinImageEdge) {
            return bytes;  // as small as this is worth making it
        }
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
}

}  // namespace

QString AppController::prepareImageForSend(const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).isLocalFile() ? QUrl(fileUrl).toLocalFile() : fileUrl;
    QImage image(localPath);
    if (image.isNull()) {
        emit imageRejected(QStringLiteral("That file is not a picture this can read."));
        return {};
    }
    return writePreparedImage(image, QFileInfo(localPath).completeBaseName());
}

bool AppController::clipboardHasImage() const
{
    const QClipboard* const clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr && !clipboard->image().isNull();
}

QString AppController::prepareClipboardImage()
{
    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const QImage image = clipboard == nullptr ? QImage() : clipboard->image();
    if (image.isNull()) {
        emit imageRejected(QStringLiteral("There is no picture in the clipboard."));
        return {};
    }
    return writePreparedImage(image, QStringLiteral("pasted"));
}

QString AppController::writePreparedImage(const QImage& image, const QString& baseName)
{
    QString format;
    const QByteArray bytes = encodedImage(image, &format);
    if (bytes.isEmpty()) {
        emit imageRejected(QStringLiteral("That picture could not be encoded."));
        return {};
    }
    const QString name = (baseName.isEmpty() ? QStringLiteral("image") : baseName) + "-"
        + QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + format;
    const QString path = scratchFile(name);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit imageRejected(QStringLiteral("Could not write the prepared picture."));
        return {};
    }
    if (out.write(bytes) != bytes.size()) {
        emit imageRejected(QStringLiteral("Could not write the prepared picture."));
        return {};
    }
    out.close();
    return QUrl::fromLocalFile(path).toString();
}

bool AppController::looksLikeImage(const QString& mime) const
{
    return mime.startsWith(QStringLiteral("image/"));
}

QString AppController::imageUrlIfSafe(const QString& localPath) const
{
    const QString path = QUrl(localPath).isLocalFile() ? QUrl(localPath).toLocalFile() : localPath;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    // Only the two formats worth showing, recognised by their own first bytes.
    constexpr qint64 kSignatureBytes = 8;
    const QByteArray head = file.read(kSignatureBytes);
    if (!looksLikePng(head) && !looksLikeJpeg(head)) {
        return {};
    }
    // And it still has to decode: a file that starts like a PNG and is not one
    // must not reach the renderer as a picture.
    const QImageReader reader(path);
    if (!reader.canRead()) {
        return {};
    }
    return QUrl::fromLocalFile(path).toString();
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

void AppController::closeAccount()
{
    if (SessionController* ctrl = activeController()) {
        removeSession(ctrl, /*deferred=*/false);
    }
    refreshAccountList();
}

}  // namespace bazarish::app
