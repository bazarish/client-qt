// Bazarish project (c) 2026
#include "AppController.hpp"
#include <thread>

#include "AppSettings.hpp"
#include "I2pRouter.hpp"
#include "Markup.hpp"
#include "Version.hpp"

#include <bazarish/I2p.hpp>
#include <bazarish/Links.hpp>

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

QString AppController::appVersion() const
{
    return QString::fromLatin1(kAppVersion);
}

void AppController::applyPendingLink()
{
    if (pendingLink_.isEmpty()) {
        return;
    }
    const QString link = pendingLink_;
    pendingLink_.clear();
    openLink(link);
}

void AppController::openLink(const QString& link)
{
    const QString uri = link.trimmed();
    if (uri.startsWith(QLatin1String(bazarish::kPairUri))) {
        emit raiseRequested();
        emit pairLinkOpened(uri);
        return;
    }
    const bool invite = uri.startsWith(QLatin1String(bazarish::kInviteUri));
    if (!invite && !uri.startsWith(QLatin1String(bazarish::kServerUri))) {
        bazarish::log::warn("asked to open a link this client does not know");
        emit linkRefused(tr("This link is not one Bazarish knows."));
        return;
    }
    emit raiseRequested();
    // Both of these act on an open account, so a link that arrives before one is
    // open waits for it rather than going nowhere.
    if (sessions_.isEmpty()) {
        pendingLink_ = uri;
        return;
    }
    if (invite) {
        emit inviteLinkOpened(uri);
        return;
    }
    emit serverLinkOpened(uri);
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
            applyPendingLink();
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
                applyPendingLink();
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

QString AppController::pairLinkProblem(const QString& link) const
{
    const QString typed = link.trimmed();
    if (typed.isEmpty()) {
        return {};
    }
    try {
        (void)bazarish::parsePairLink(typed.toStdString());
    } catch (const std::exception&) {
        return tr("This is not a pairing link.");
    }
    return {};
}

void AppController::failPairing(const QString& reason)
{
    pairing_ = false;
    pairNeedsCode_ = false;
    pairCancel_.reset();
    pairEndpoint_.reset();
    pairStatus_ = reason;
    pairProgress_ = kProgressUnknown;
    emit pairingChanged();
    emit pairingFinished(false);
}

void AppController::startPairing(const QString& link, const QString& atRestPassphrase)
{
    if (pairing_) {
        return;
    }
    bazarish::PairLink parsed;
    try {
        parsed = bazarish::parsePairLink(link.trimmed().toStdString());
    } catch (const std::exception& error) {
        failPairing(QString::fromUtf8(error.what()));
        return;
    }

    pairDest_ = QString::fromStdString(parsed.dest);
    pairAtRest_ = atRestPassphrase;
    pairCancel_ = std::make_shared<std::atomic<bool>>(false);
    pairing_ = true;
    pairNeedsCode_ = false;
    pairProgress_ = kProgressUnknown;
    pairStatus_ = client::sharedI2pRouterIfRunning() == nullptr
        ? tr("Starting the I2P router")
        : tr("Building your I2P tunnels");
    emit pairingChanged();

    const std::vector<std::string> reseeds = parsed.reseeds;
    const std::string dest = pairDest_.toStdString();
    const std::shared_ptr<std::atomic<bool>> cancel = pairCancel_;
    std::thread([this, reseeds, dest, cancel]() {
        QString failure;
        std::shared_ptr<bazarish::i2p::Endpoint> endpoint;
        const auto say = [this, cancel](const QString& stage) {
            QMetaObject::invokeMethod(
                this,
                [this, cancel, stage]() {
                    if (pairCancel_ != cancel) {
                        return;
                    }
                    pairStatus_ = stage;
                    emit pairingChanged();
                },
                Qt::QueuedConnection);
        };
        try {
            if (client::applyLinkReseed(i2pRoot(), reseeds)) {
                say(tr("Reseeding from the link"));
            }
            if (client::bootstrapI2pRouter(i2pRoot()) == client::I2pBootstrap::eEmpty) {
                failure = tr("No reseed answered. This device has no network database.");
            } else {
                say(tr("Building your I2P tunnels"));
                endpoint = client::openPairLink(client::sharedI2pRouter(i2pRoot()),
                    client::tunnelPrivacy(), client::kPairingOwner);
                if (!endpoint->waitReady(
                        std::chrono::seconds(client::kPairOwnTunnelsSeconds))) {
                    endpoint.reset();
                    failure = tr("This device could not build I2P tunnels.");
                }
            }
            if (endpoint) {
                say(tr("Reaching the other device"));
                const std::unique_ptr<bazarish::i2p::Stream> reached
                    = endpoint->connect(dest, std::chrono::seconds(client::kPairDialSeconds));
                if (!reached) {
                    endpoint.reset();
                    failure = tr("Cannot reach the other device.");
                } else {
                    reached->close();
                }
            }
        } catch (const std::exception& error) {
            failure = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, cancel, endpoint, failure]() {
                if (pairCancel_ != cancel) {
                    return;
                }
                if (!failure.isEmpty()) {
                    failPairing(failure);
                    return;
                }
                pairEndpoint_ = endpoint;
                pairNeedsCode_ = true;
                pairStatus_ = tr("Enter the code shown on the other device");
                emit pairingChanged();
            },
            Qt::QueuedConnection);
    }).detach();
}

void AppController::submitPairCode(const QString& code)
{
    if (!pairNeedsCode_ || !pairEndpoint_ || !client::isPairCode(code.toStdString())) {
        return;
    }
    pairNeedsCode_ = false;
    pairProgress_ = kProgressUnknown;
    pairStatus_ = tr("Receiving the account");
    emit pairingChanged();

    const std::shared_ptr<bazarish::i2p::Endpoint> endpoint = pairEndpoint_;
    const std::shared_ptr<std::atomic<bool>> cancel = pairCancel_;
    const std::string dest = pairDest_.toStdString();
    const std::string want = code.toStdString();
    const QString atRest = pairAtRest_;
    std::thread([this, endpoint, cancel, dest, want, atRest]() {
        QString failure;
        bool wrongCode = false;
        int triesLeft = 0;
        std::string id;
        try {
            const client::PairProgressFn onProgress
                = [this, cancel](const std::uint64_t done, const std::uint64_t total) {
                      QMetaObject::invokeMethod(
                          this,
                          [this, cancel, done, total]() {
                              if (pairCancel_ != cancel) {
                                  return;
                              }
                              pairProgress_ = total > 0 ? static_cast<double>(done)
                                      / static_cast<double>(total)
                                                        : kProgressUnknown;
                              emit pairingChanged();
                          },
                          Qt::QueuedConnection);
                  };
            const client::PairFetchResult got
                = client::fetchPairBundle(*endpoint, dest, want, onProgress, *cancel);
            if (got.wrongCode) {
                wrongCode = true;
                triesLeft = got.triesLeft;
            } else {
                id = manager_->import(std::string{}, got.bundle, want, atRest.toStdString()).id;
            }
        } catch (const std::exception& error) {
            failure = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, cancel, failure, wrongCode, triesLeft, id, atRest]() {
                if (pairCancel_ != cancel) {
                    return;
                }
                if (wrongCode) {
                    pairNeedsCode_ = true;
                    pairStatus_ = tr("Wrong code. %1 tries left").arg(triesLeft);
                    emit pairingChanged();
                    return;
                }
                if (!failure.isEmpty()) {
                    failPairing(failure);
                    return;
                }
                pairing_ = false;
                pairCancel_.reset();
                pairEndpoint_.reset();
                pairProgress_ = 1.0;
                pairStatus_ = tr("The account is on this device");
                emit pairingChanged();
                emit pairingFinished(true);
                refreshAccountList();
                openSession(QString::fromStdString(id), atRest, /*makeActive=*/true);
            },
            Qt::QueuedConnection);
    }).detach();
}

void AppController::cancelPairing()
{
    if (!pairing_) {
        return;
    }
    if (pairCancel_) {
        pairCancel_->store(true);
    }
    pairCancel_.reset();
    pairEndpoint_.reset();
    pairing_ = false;
    pairNeedsCode_ = false;
    pairStatus_.clear();
    pairProgress_ = kProgressUnknown;
    emit pairingChanged();
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
