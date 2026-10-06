// Bazarish project (c) 2026
#include "AppController.hpp"
#include <thread>

#include "AppSettings.hpp"
#include "I2pRouter.hpp"
#include "Markup.hpp"

#include <bazarish/I2p.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QBuffer>
#include <QClipboard>
#include <QColor>
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

AppController::AppController(QObject* parent)
    : QObject(parent)
    , manager_(std::make_unique<client::AccountManager>(accountsRoot()))
    , ringtone_(soundFolder())
{
    connect(&ringtone_, &Ringtone::levelChanged, this, [this](const qreal level) {
        ringLevel_ = level;
        emit ringLevelChanged();
    });
    refreshAccountList();
    loadSettings();
    loadOfflineSet();
    openAllAccounts();
    const QString last = readLastActive();
    if (sessionFor(last) != nullptr) {
        activeId_ = last;
    } else if (!sessions_.isEmpty()) {
        activeId_ = sessions_.first()->accountId();
    }
    if (!activeId_.isEmpty()) {
        writeLastActive(activeId_);
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
    return QString::fromStdString(AppSettings::instance().activeAccount());
}

void AppController::writeLastActive(const QString& id) const
{
    AppSettings::instance().setActiveAccount(id.toStdString());
}

void AppController::loadSettings()
{
    notifications_ = AppSettings::instance().notifications();
    backgroundTasks_ = AppSettings::instance().backgroundTasks();
}

void AppController::persistSettings() const
{
    AppSettings::instance().setNotifications(notifications_);
}

QString AppController::notificationTitle(const SessionController* const ctrl) const
{
    if (ctrl == nullptr || ctrl->displayName().isEmpty()) {
        return QStringLiteral("Bazarish");
    }
    return ctrl->displayName();
}

std::filesystem::path AppController::accountsFolder()
{
    return accountsRoot();
}

QString AppController::soundFolder()
{
    return QString::fromStdString(appRoot().string());
}

void AppController::setNotificationsEnabled(const bool on)
{
    if (notifications_ == on) {
        return;
    }
    notifications_ = on;
    persistSettings();
    emit notificationsEnabledChanged();
    updateRinging();
}

void AppController::setBackgroundTasksVisible(const bool on)
{
    if (backgroundTasks_ == on) {
        return;
    }
    backgroundTasks_ = on;
    AppSettings::instance().setBackgroundTasks(backgroundTasks_);
    emit backgroundTasksVisibleChanged();
}

void AppController::updateRinging()
{
    const SessionController* ringing = nullptr;
    if (notifications_) {
        for (const SessionController* const ctrl : sessions_) {
            if (ctrl->callState() == QLatin1String("incoming")) {
                ringing = ctrl;
                break;
            }
        }
    }
    const QString account = ringing ? ringing->accountId() : QString();
    const QString peer = ringing ? ringing->callPeerName() : QString();
    const QString fingerprint = ringing ? ringing->callPeer() : QString();
    const QString accountName = ringing ? ringing->displayName() : QString();
    if (account != ringingAccount_ || peer != ringingPeer_
        || fingerprint != ringingPeerFingerprint_ || accountName != ringingAccountName_) {
        ringingAccount_ = account;
        ringingPeer_ = peer;
        ringingPeerFingerprint_ = fingerprint;
        ringingAccountName_ = accountName;
        emit ringingChanged();
    }
    if (ringing != nullptr) {
        ringtone_.start();
    } else {
        ringtone_.stop();
    }
}

void AppController::answerRinging()
{
    SessionController* const ctrl = sessionFor(ringingAccount_);
    if (ctrl == nullptr) {
        return;
    }
    if (ctrl->accountId() != activeId_) {
        switchTo(ctrl->accountId());
    }
    emit raiseRequested();
    ctrl->acceptCall();
}

void AppController::declineRinging()
{
    if (SessionController* const ctrl = sessionFor(ringingAccount_)) {
        ctrl->declineCall();
    }
}

QString AppController::i2pdVersion() const
{
    return QString::fromStdString(bazarish::i2p::routerVersion());
}

void AppController::loadOfflineSet()
{
    offline_.clear();
    for (const std::string& id : AppSettings::instance().offlineAccounts()) {
        offline_.insert(QString::fromStdString(id));
    }
}

void AppController::persistOfflineSet() const
{
    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(offline_.size()));
    for (const QString& id : offline_) {
        ids.push_back(id.toStdString());
    }
    AppSettings::instance().setOfflineAccounts(std::move(ids));
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
            const QString id = QString::fromStdString(info.id);
            rows.push_back(AccountListRow{id, QString::fromStdString(info.name),
                QString::fromStdString(info.fingerprint), info.encrypted,
                sessionFor(id) != nullptr});
        }
    } catch (const std::exception& error) {
        bazarish::log::warn("account list incomplete: {}", error.what());
    }
    haveAccounts_ = !rows.isEmpty();
    accountRows_ = rows;
    accountList_.setAccounts(std::move(rows));
    emit accountListChanged();
}

void AppController::refreshAccountRows()
{
    bool changed = false;
    for (AccountListRow& row : accountRows_) {
        const SessionController* const ctrl = sessionFor(row.id);
        if (row.open != (ctrl != nullptr)) {
            row.open = ctrl != nullptr;
            changed = true;
        }
        if (ctrl == nullptr) {
            continue;
        }
        const QString fingerprint = ctrl->fingerprint();
        const QString name = ctrl->displayName();
        if ((!fingerprint.isEmpty() && row.fingerprint != fingerprint)
            || (!name.isEmpty() && row.name != name)) {
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

void AppController::refreshAccounts()
{
    refreshAccountRows();
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
            row.activeFacade = ctrl->activeFacade();
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

void AppController::openSession(const QString& id, const QString& passphrase,
    const bool makeActive, const bool startOnline)
{
    if (sessionFor(id) != nullptr) {
        if (makeActive) {
            setActive(id);
            emit accountOpened();
        }
        return;
    }

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
        removeSession(ctrl);
        if (unlockingId_ == id) {
            refreshAccounts();
            emit unlockFailed(error);
            return;
        }
        if (pendingDeleteId_ == id) {
            pendingDeleteId_.clear();
            bazarish::log::warn("account {} could not be opened to delete it: {}",
                id.toStdString(), error.toStdString());
            emit accountDeleteFailed(id, error, /*profileNotOpened=*/true);
            return;
        }
        emit accountOpenFailed(error);
    });
    connect(ctrl, &SessionController::messageNotification, this,
        [this, ctrl](const QString& peer, const QString& fromName) {
            emit notificationRequested(ctrl->accountId(), peer, notificationTitle(ctrl),
                tr("New message from %1").arg(fromName));
        });
    connect(ctrl, &SessionController::reactionNotification, this,
        [this, ctrl](const QString& peer, const QString& fromName, const QString& emoji) {
            emit reactionNotificationRequested(ctrl->accountId(), peer, notificationTitle(ctrl),
                tr("%1 reacted %2").arg(fromName, emoji));
        });
    connect(ctrl, &SessionController::callChanged, this, [this]() { updateRinging(); });
    connect(ctrl, &SessionController::unreadTotalChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::onlineChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::reachableChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::connectedChanged, this, &AppController::refreshAccounts);
    connect(ctrl, &SessionController::facadeInfoChanged, this, &AppController::refreshAccounts);

    try {
        const QString file
            = QString::fromStdString(manager_->fileFor(id.toStdString()).string());
        ctrl->open(file, id, passphrase, startOnline);
    } catch (const std::exception& e) {
        removeSession(ctrl);
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
        if (!info.encrypted && offline_.constFind(id) == offline_.cend()) {
            openSession(id, {}, /*makeActive=*/false);
        }
    }
}

void AppController::removeSession(SessionController* const ctrl)
{
    const QString id = ctrl->accountId();
    sessions_.removeAll(ctrl);
    if (activeId_ == id) {
        activeId_ = sessions_.isEmpty() ? QString() : sessions_.first()->accountId();
        writeLastActive(activeId_);
        emit sessionChanged();
    }
    updateRinging();
    refreshAccounts();
    ++closingCount_;
    connect(ctrl, &SessionController::closed, this, [this, ctrl, id]() {
        --closingCount_;
        ctrl->deleteLater();
        onSessionClosed(id);
        if (exiting_ && closingCount_ == 0) {
            emit readyToExit();
        }
    });
    ctrl->beginShutdown();
}

void AppController::onSessionClosed(const QString& id)
{
    if (pendingRemovals_.remove(id)) {
        removeAccountFiles(id);
    }
    if (deletingId_ == id) {
        deletingId_.clear();
        emit deletingChanged();
    }
}

void AppController::removeAccountFiles(const QString& id)
{
    try {
        manager_->remove(id.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::warn("account directory not removed: {}", error.what());
    }
    refreshAccountList();
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
    const bool wasOff = offline_.constFind(id) != offline_.cend();
    const bool bringOnline = !wasOff || (unlockingId_ == id && unlockToBringOnline_);
    unlockingId_ = id;
    openSession(id, passphrase, /*makeActive=*/true, /*startOnline=*/bringOnline);
    if (sessionFor(id) == nullptr) {
        return;
    }
    unlockingId_.clear();
    unlockToBringOnline_ = false;
    emit accountUnlocked(id);
    if (pendingDeleteId_ == id) {
        deleteAccount(id);
        return;
    }
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
    pendingDeleteId_.clear();
    refreshAccounts();
}

void AppController::importAccount(const QString& name, const QString& fileUrl,
    const QString& password, const QString& atRestPassphrase)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    OperationRow row;
    row.id = QStringLiteral("restore");
    row.kind = QStringLiteral("account");
    row.title = tr("Restoring your account");
    row.status = tr("Opening the backup…");
    row.state = eOpRunning;
    row.startedAt = QDateTime::currentMSecsSinceEpoch();
    operations_.upsert(row);
    emit operationsChanged();

    std::thread([this, name, localPath, password, atRestPassphrase]() {
        std::string id;
        QString failure;
        try {
            const client::AccountInfo info = manager_->import(name.toStdString(),
                localPath.toStdString(), password.toStdString(), atRestPassphrase.toStdString());
            id = info.id;
        } catch (const std::exception& e) {
            failure = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(this,
            [this, id, failure, atRestPassphrase]() {
                operations_.update(QStringLiteral("restore"),
                    failure.isEmpty() ? tr("Restored.") : failure, {}, -1,
                    failure.isEmpty() ? eOpDone : eOpFailed);
                emit operationsChanged();
                if (!failure.isEmpty()) {
                    emit createFailed(failure);
                    return;
                }
                refreshAccountList();
                openSession(QString::fromStdString(id), atRestPassphrase, /*makeActive=*/true);
            },
            Qt::QueuedConnection);
    }).detach();
}

void AppController::deleteAccount(const QString& id)
{
    if (!deletingId_.isEmpty()) {
        return;
    }
    deletingId_ = id;
    emit deletingChanged();
    SessionController* ctrl = sessionFor(id);
    if (ctrl == nullptr) {
        for (const AccountListRow& info : accountRows_) {
            if (info.id == id && info.encrypted) {
                deletingId_.clear();
                emit deletingChanged();
                emit accountDeleteNeedsUnlock(id, info.name);
                return;
            }
        }
        pendingDeleteId_ = id;
        openSession(id, {}, /*makeActive=*/false);
        ctrl = sessionFor(id);
        if (ctrl == nullptr) {
            deletingId_.clear();
            emit deletingChanged();
            return;
        }
    }
    pendingDeleteId_.clear();
    if (ctrl->configuredFacades().isEmpty()) {
        forgetAccountLocally(id);
        return;
    }
    const auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(ctrl, &SessionController::accountClosedOnServer, this,
        [this, id, connection](const bool ok, const QString& error) {
            disconnect(*connection);
            if (!ok) {
                deletingId_.clear();
                emit deletingChanged();
                emit accountDeleteFailed(id, error, /*profileNotOpened=*/false);
                return;
            }
            forgetAccountLocally(id);
        });
    ctrl->closeAccountOnServer();
}

void AppController::deleteAccountAfterUnlock(const QString& id)
{
    pendingDeleteId_ = id;
    openSession(id, {}, /*makeActive=*/false);
    if (sessionFor(id) != nullptr) {
        deleteAccount(id);
    }
}

void AppController::forgetAccountLocally(const QString& id)
{
    if (SessionController* const ctrl = sessionFor(id)) {
        deletingId_ = id;
        emit deletingChanged();
        pendingRemovals_.insert(id);
        removeSession(ctrl);
        return;
    }
    removeAccountFiles(id);
}

void AppController::openConversationOf(const QString& accountId, const QString& peer)
{
    SessionController* const ctrl = sessionFor(accountId);
    if (ctrl == nullptr) {
        return;
    }
    setActive(accountId);
    ctrl->openConversation(peer);
}

void AppController::switchTo(const QString& id)
{
    if (sessionFor(id) != nullptr) {
        setActive(id);
        return;
    }
    const bool wasOff = offline_.constFind(id) != offline_.cend();
    openSession(id, {}, /*makeActive=*/true, /*startOnline=*/!wasOff);
}

void AppController::setOnline(const QString& id, bool on)
{
    SessionController* ctrl = sessionFor(id);
    if (!on) {
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
    unlockToBringOnline_ = true;
    openSession(id, {}, /*makeActive=*/false);
    if (unlockingId_.isEmpty()) {
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
    if (on && underTempDirectory(client::AccountManager::portableRoot())) {
        emit createFailed(tr("The application is running from a temporary directory (%1), which is cleared on reboot. Move it to a folder of its own first.")
                .arg(QString::fromStdString(
                    client::AccountManager::portableRoot().parent_path().string())));
        return;
    }
    if (on && !directoryIsWritable(client::AccountManager::portableRoot().parent_path())) {
        emit createFailed(tr("Cannot write beside the application (%1). Move it somewhere writable and try again.")
                .arg(QString::fromStdString(
                    client::AccountManager::portableRoot().parent_path().string())));
        return;
    }
    closeAllSessions();
    std::error_code error;
    if (fs::exists(from, error) && !fs::is_empty(from, error)) {
        if (fs::exists(to, error) && !fs::is_empty(to, error)) {
            emit createFailed(tr("There is already data at %1 - move or remove it first, so "
                                 "nothing is overwritten.")
                    .arg(QString::fromStdString(to.string())));
            return;
        }
        fs::create_directories(to.parent_path(), error);
        fs::rename(from, to, error);
        if (error) {
            error.clear();
            fs::copy(from, to, fs::copy_options::recursive, error);
            if (error) {
                emit createFailed(tr("Could not move the data: %1")
                    .arg(QString::fromStdString(error.message())));
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
    emit restartRequired(on
            ? tr("Your data now lives beside the app. Bazarish has to be started "
                             "again to use it.")
            : tr("Your data moved back to your user folder. Bazarish has to be "
                             "started again to use it."));
}

bool AppController::clipboardHasImage() const
{
    const QClipboard* const clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr && !clipboard->image().isNull();
}

QString AppController::markupHtml(const QString& text, const QColor& actionColor,
    const QColor& chipColor, const QColor& codeColor, const QColor& codeTextColor) const
{
    return markup::toHtml(text,
        markup::Colors{actionColor.name(), chipColor.name(), codeColor.name(),
            codeTextColor.name()});
}

QString AppController::markupPlain(const QString& text) const
{
    return markup::toPlain(text);
}

void AppController::copyText(const QString& text) const
{
    if (QClipboard* const clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void AppController::retranslate()
{
    for (SessionController* const session : sessions_) {
        if (session != nullptr) {
            session->retranslate();
        }
    }
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

void AppController::prepareForExit()
{
    if (exiting_) {
        return;
    }
    exiting_ = true;
    emit readyToExit();
}

void AppController::closeAllSessions()
{
    const QVector<SessionController*> open = sessions_;
    for (SessionController* const ctrl : open) {
        removeSession(ctrl);
    }
    refreshAccounts();
}

}  // namespace bazarish::app
