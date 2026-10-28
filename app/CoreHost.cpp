// Bazarish project (c) 2026
#include "CoreHost.hpp"

#include "AppSettings.hpp"
#include "AudioRoute.hpp"
#include "CoreAccount.hpp"
#include "DevicePairing.hpp"
#include "Documents.hpp"
#include "I2pRouter.hpp"
#include "PathText.hpp"
#include "Wire.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidCallAudio.hpp"
#endif

#include <bazarish/I2p.hpp>
#include <bazarish/Links.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QMetaType>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace bazarish::app {

namespace {

const QString kAppScope = QStringLiteral("app");
const QString kRestoreOperation = QStringLiteral("restore");

const int kAccountListMetaType
    = qRegisterMetaType<QVector<AccountListRow>>("QVector<AccountListRow>");
const int kAccountRowsMetaType = qRegisterMetaType<QVector<AccountRow>>("QVector<AccountRow>");
const int kOperationRowMetaType = qRegisterMetaType<OperationRow>("OperationRow");

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

CoreHost::CoreHost(QObject* const parent)
    : QObject(parent)
    , manager_(std::make_unique<client::AccountManager>(accountsRoot()))
{
    connect(&ringtone_, &Ringtone::levelChanged, &events_, &AppEvents::ringLevel);
    connect(&i2p_, &I2pService::tunnelsNeedRebuild, this, &CoreHost::rebuildI2pLinks);
    connect(&log_, &OperationLog::changed, &events_, &AppEvents::opChanged);
    connect(&log_, &OperationLog::removed, &events_, &AppEvents::opRemoved);

    connect(&commands_, &AppCommands::refreshAccountList, this, &CoreHost::refreshAccountList);
    connect(&commands_, &AppCommands::openAccount, this, &CoreHost::openAccount);
    connect(&commands_, &AppCommands::switchTo, this, &CoreHost::switchTo);
    connect(&commands_, &AppCommands::setOnline, this, &CoreHost::setOnline);
    connect(&commands_, &AppCommands::createAccount, this, &CoreHost::createAccount);
    connect(&commands_, &AppCommands::importAccount, this, &CoreHost::importAccount);
    connect(&commands_, &AppCommands::startPairing, this, &CoreHost::startPairing);
    connect(&commands_, &AppCommands::submitPairCode, this, &CoreHost::submitPairCode);
    connect(&commands_, &AppCommands::cancelPairing, this, &CoreHost::cancelPairing);
    connect(&commands_, &AppCommands::deleteAccount, this, &CoreHost::deleteAccount);
    connect(&commands_, &AppCommands::deleteAccountAfterUnlock, this,
        &CoreHost::deleteAccountAfterUnlock);
    connect(&commands_, &AppCommands::forgetAccountLocally, this,
        &CoreHost::forgetAccountLocally);
    connect(&commands_, &AppCommands::cancelUnlock, this, &CoreHost::cancelUnlock);
    connect(&commands_, &AppCommands::setNotificationsEnabled, this,
        &CoreHost::setNotificationsEnabled);
    connect(&commands_, &AppCommands::setBackgroundTasksVisible, this,
        &CoreHost::setBackgroundTasksVisible);
    connect(&commands_, &AppCommands::setSpeakerOn, this, &CoreHost::setSpeakerOn);
    connect(&commands_, &AppCommands::setPortable, this, &CoreHost::setPortable);
    connect(&commands_, &AppCommands::closeAllSessions, this, &CoreHost::closeAllSessions);
    connect(&commands_, &AppCommands::windowActive, this, &CoreHost::setWindowActive);
    connect(&commands_, &AppCommands::callScreenShown, this, &CoreHost::setCallScreenShown);
    connect(&commands_, &AppCommands::quit, this, &CoreHost::prepareForExit);
    connect(&commands_, &AppCommands::setLanguage, this, &CoreHost::setLanguage);
    connect(&commands_, &AppCommands::setLightTheme, this, &CoreHost::setLightTheme);
    connect(&commands_, &AppCommands::setCamera, this, &CoreHost::setCamera);
    connect(&commands_, &AppCommands::setScreenCapture, this, &CoreHost::setScreenCapture);
    connect(&commands_, &AppCommands::chooseAudio, this, &CoreHost::chooseAudio);

    connect(&relay_, &Relay::broken, this, &CoreHost::windowBroken);
    relay_.sendFrom(kAppScope, events_, kAppLatches);
    relay_.deliverTo(kAppScope, commands_);
    i2p_.attach(relay_);
}

CoreHost::~CoreHost()
{
#ifdef Q_OS_ANDROID
    bazarish::i2p::onCarryReturned({});
#endif
}

void CoreHost::attachWindow(ChannelEnd& channel)
{
    relay_.attach(channel);
    for (const CoreAccount* const account : sessions_) {
        emit events_.sessionStarted(account->accountId());
    }
    relay_.replay();
    for (const OperationRow& row : log_.rows()) {
        emit events_.opChanged(row);
    }
    for (CoreAccount* const account : sessions_) {
        account->replayOperations();
    }
    emit events_.started();
}

void CoreHost::detachWindow()
{
    relay_.detach();
    setWindowActive(false);
    setCallScreenShown(false);
}

void CoreHost::setWindowActive(const bool active)
{
    if (windowActive_ == active) {
        return;
    }
    windowActive_ = active;
    emit windowActiveChanged();
    tellPresenceFocus();
}

void CoreHost::tellPresenceFocus()
{
    for (CoreAccount* const account : sessions_) {
        account->setPresenceFocused(windowActive_ && account->accountId() == activeId_);
    }
}

void CoreHost::setCallScreenShown(const bool shown)
{
    if (callScreenShown_ == shown) {
        return;
    }
    callScreenShown_ = shown;
    emit callScreenShownChanged();
}

void CoreHost::declineRinging()
{
    if (CoreAccount* const account = sessionFor(ringingAccount_)) {
        emit account->commands().declineCall();
    }
}

void CoreHost::networkChanged()
{
    bazarish::log::info("a network became the default: reconnecting at once");
    bazarish::i2p::networkChanged();
}

void CoreHost::start()
{
#ifdef Q_OS_ANDROID
    // A gateway answering again wakes a mail wait as a new network does.
    bazarish::i2p::onCarryReturned([this]() {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                for (CoreAccount* const session : sessions_) {
                    session->networkChanged();
                }
            },
            Qt::QueuedConnection);
    });
#endif
    i2p_.announce();
    notifications_ = AppSettings::instance().notifications();
    backgroundTasks_ = AppSettings::instance().backgroundTasks();
    emit events_.settings(notifications_, backgroundTasks_);
    refreshAccountList();
    loadOfflineSet();
    openAllAccounts();
    const QString last = QString::fromStdString(AppSettings::instance().activeAccount());
    if (sessionFor(last) != nullptr) {
        setActive(last);
    } else if (!sessions_.isEmpty()) {
        setActive(sessions_.first()->accountId());
    }
    refreshAccounts();
}

CoreAccount* CoreHost::sessionFor(const QString& id) const
{
    for (CoreAccount* const account : sessions_) {
        if (account->accountId() == id) {
            return account;
        }
    }
    return nullptr;
}

QString CoreHost::accountName(const CoreAccount* const account) const
{
    if (!account->displayName().isEmpty()) {
        return account->displayName();
    }
    for (const AccountListRow& info : accountRows_) {
        if (info.id == account->accountId()) {
            return info.name;
        }
    }
    return {};
}

QString CoreHost::soundFolder()
{
    return pathText(appRoot());
}

void CoreHost::setNotificationsEnabled(const bool on)
{
    if (notifications_ == on) {
        return;
    }
    notifications_ = on;
    AppSettings::instance().setNotifications(notifications_);
    emit events_.settings(notifications_, backgroundTasks_);
    emit notificationsEnabledChanged();
    updateRinging();
}

void CoreHost::setBackgroundTasksVisible(const bool on)
{
    if (backgroundTasks_ == on) {
        return;
    }
    backgroundTasks_ = on;
    AppSettings::instance().setBackgroundTasks(backgroundTasks_);
    emit events_.settings(notifications_, backgroundTasks_);
}

void CoreHost::updateRinging()
{
    const CoreAccount* ringing = nullptr;
    if (notifications_) {
        for (const CoreAccount* const account : sessions_) {
            if (account->callState() == QLatin1String("incoming")) {
                ringing = account;
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
        emit events_.ringing(account, peer, fingerprint, accountName);
        emit ringingChanged();
    }
    if (ringing != nullptr) {
        ringtone_.start();
    } else {
        ringtone_.stop();
    }
    bool live = false;
    bool talking = false;
    for (const CoreAccount* const session : sessions_) {
        live = live || session->callState() != QLatin1String("idle");
        talking = talking || session->callState() == QLatin1String("active")
            || session->callState() == QLatin1String("outgoing");
    }
    if (live != callLive_ || talking != callTalking_) {
        if (live && !callLive_) {
            setSpeakerOn(false);
        }
        callLive_ = live;
        callTalking_ = talking;
        emit events_.call(callLive_);
        emit callLiveChanged();
    }
}

void CoreHost::setSpeakerOn(const bool on)
{
    if (speakerOn_ == on) {
        return;
    }
    speakerOn_ = on;
#ifdef Q_OS_ANDROID
    AndroidCallAudio::setSpeaker(speakerOn_);
#endif
    emit events_.speakerOn(speakerOn_);
    emit speakerOnChanged();
}

void CoreHost::loadOfflineSet()
{
    offline_.clear();
    for (const std::string& id : AppSettings::instance().offlineAccounts()) {
        offline_.insert(QString::fromStdString(id));
    }
}

void CoreHost::persistOfflineSet() const
{
    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(offline_.size()));
    for (const QString& id : offline_) {
        ids.push_back(id.toStdString());
    }
    AppSettings::instance().setOfflineAccounts(std::move(ids));
}

void CoreHost::setAccountOffline(const QString& id, const bool offline)
{
    if (offline_.contains(id) == offline) {
        return;
    }
    if (offline) {
        offline_.insert(id);
    } else {
        offline_.remove(id);
    }
    persistOfflineSet();
}

void CoreHost::refreshAccountList()
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
    accountRows_ = rows;
    emit events_.accountList(accountRows_);
}

void CoreHost::refreshAccountRows()
{
    bool changed = false;
    for (AccountListRow& row : accountRows_) {
        const CoreAccount* const account = sessionFor(row.id);
        if (row.open != (account != nullptr)) {
            row.open = account != nullptr;
            changed = true;
        }
        if (account == nullptr) {
            continue;
        }
        const QString fingerprint = account->fingerprint();
        const QString name = account->displayName();
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
        emit events_.accountList(accountRows_);
    }
}

void CoreHost::refreshAccounts()
{
    refreshAccountRows();
    QVector<AccountRow> rows;
    for (const AccountListRow& info : accountRows_) {
        AccountRow row;
        row.id = info.id;
        row.encrypted = info.encrypted;
        if (const CoreAccount* const account = sessionFor(info.id)) {
            row.open = true;
            row.active = info.id == activeId_;
            row.online = account->online();
            row.connected = account->reachable();
            row.unread = account->unreadTotal();
            row.activeFacade = account->activeFacade();
            row.name = account->displayName().isEmpty() ? info.name : account->displayName();
            row.fingerprint
                = account->fingerprint().isEmpty() ? info.fingerprint : account->fingerprint();
        } else {
            row.name = info.name;
            row.fingerprint = info.fingerprint;
        }
        rows.push_back(std::move(row));
    }
    accountStatuses_ = rows;
    const bool anyOnline = std::any_of(sessions_.cbegin(), sessions_.cend(),
        [](const CoreAccount* const account) { return account->online(); });
    client::setWarmDestsWanted(anyOnline);
    emit events_.accounts(accountStatuses_);
    emit accountsChanged();
}

void CoreHost::setActive(const QString& id)
{
    if (id == activeId_) {
        return;
    }
    activeId_ = id;
    AppSettings::instance().setActiveAccount(id.toStdString());
    emit events_.activeChanged(id);
    refreshAccounts();
    tellPresenceFocus();
}

void CoreHost::openSession(const QString& id, const QString& passphrase, const bool makeActive,
    const bool startOnline)
{
    if (sessionFor(id) != nullptr) {
        if (makeActive) {
            setActive(id);
            emit events_.accountReady(id);
        }
        return;
    }

    if (passphrase.isEmpty() && manager_->exists(id.toStdString())) {
        for (const AccountListRow& info : accountRows_) {
            if (info.id == id && info.encrypted) {
                unlockingId_ = id;
                emit events_.needPassphrase(id, info.name);
                return;
            }
        }
    }

    auto* const account = new CoreAccount(id, this);
    sessions_.append(account);
    relay_.sendFrom(id, account->events(), kAccountLatches);
    relay_.deliverTo(id, account->commands());
    emit events_.sessionStarted(id);

    connect(&account->events(), &AccountEvents::opened, this, [this, id, makeActive]() {
        refreshAccounts();
        tellPresenceFocus();
        if (makeActive && id == activeId_) {
            emit events_.accountReady(id);
        }
    });
    connect(&account->events(), &AccountEvents::openFailed, this,
        [this, account, id](const QString& error) {
            removeSession(account);
            if (unlockingId_ == id) {
                emit events_.unlockFailed(error);
                return;
            }
            if (pendingDeleteId_ == id) {
                pendingDeleteId_.clear();
                bazarish::log::warn("account {} could not be opened to delete it: {}",
                    id.toStdString(), error.toStdString());
                emit events_.accountDeleteFailed(id, error, /*profileNotOpened=*/true);
                return;
            }
            emit events_.accountOpenFailed(error);
        });
    connect(account, &CoreAccount::messageNotification, this,
        [this, account](const QString& peer, const QString& fromName) {
            const QString body = sessions_.size() > 1
                ? tr("New message for %1").arg(accountName(account)) : tr("New message");
            emit notificationRequested(account->accountId(), peer, fromName, body);
        });
    connect(account, &CoreAccount::reactionNotification, this,
        [this, account](const QString& peer, const QString& fromName, const QString& emoji) {
            const QString body = sessions_.size() > 1
                ? tr("Reaction %1 for %2").arg(emoji, accountName(account))
                : tr("Reaction %1").arg(emoji);
            emit reactionNotificationRequested(account->accountId(), peer, fromName, body);
        });
    connect(account, &CoreAccount::callChanged, this, &CoreHost::updateRinging);
    connect(account, &CoreAccount::changed, this, &CoreHost::refreshAccounts);

    QString file;
    try {
        file = pathText(manager_->fileFor(id.toStdString()));
    } catch (const std::exception& error) {
        removeSession(account);
        if (unlockingId_ == id) {
            emit events_.unlockFailed(QString::fromUtf8(error.what()));
            return;
        }
        emit events_.accountOpenFailed(QString::fromUtf8(error.what()));
        return;
    }
    account->open(file, passphrase, startOnline);
    if (sessionFor(id) == nullptr) {
        return;
    }
    if (makeActive) {
        setActive(id);
    }
    refreshAccounts();
}

void CoreHost::openAllAccounts()
{
    const QVector<AccountListRow> rows = accountRows_;
    for (const AccountListRow& info : rows) {
        if (!info.encrypted && !offline_.contains(info.id)) {
            openSession(info.id, {}, /*makeActive=*/false);
        }
    }
}

void CoreHost::removeSession(CoreAccount* const account)
{
    const QString id = account->accountId();
    sessions_.removeAll(account);
    if (activeId_ == id) {
        setActive(sessions_.isEmpty() ? QString() : sessions_.first()->accountId());
    }
    updateRinging();
    refreshAccounts();
    relay_.forget(id);
    emit events_.sessionEnded(id);
    connect(&account->events(), &AccountEvents::closed, this, [this, account, id]() {
        account->deleteLater();
        onSessionClosed(id);
    });
    account->beginShutdown();
}

void CoreHost::onSessionClosed(const QString& id)
{
    if (pendingRemovals_.remove(id)) {
        removeAccountFiles(id);
    }
    if (deletingId_ == id) {
        setDeleting({});
    }
}

void CoreHost::setDeleting(const QString& id)
{
    deletingId_ = id;
    emit events_.deleting(id);
}

void CoreHost::removeAccountFiles(const QString& id)
{
    try {
        manager_->remove(id.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::warn("account directory not removed: {}", error.what());
    }
    refreshAccountList();
    refreshAccounts();
}

void CoreHost::createAccount(const QString& name, const QString& passphrase)
{
    std::string id;
    try {
        id = manager_->create(
            name.toStdString(), bazarish::AccountKind::eUser, passphrase.toStdString()).id;
    } catch (const std::exception& error) {
        emit events_.createFailed(QString::fromUtf8(error.what()));
        return;
    }
    refreshAccountList();
    openSession(QString::fromStdString(id), passphrase, /*makeActive=*/true);
}

void CoreHost::openAccount(const QString& id, const QString& passphrase)
{
    const bool wasOff = offline_.contains(id);
    const bool bringOnline = !wasOff || (unlockingId_ == id && unlockToBringOnline_);
    unlockingId_ = id;
    openSession(id, passphrase, /*makeActive=*/true, /*startOnline=*/bringOnline);
    CoreAccount* const account = sessionFor(id);
    if (account == nullptr) {
        return;
    }
    unlockingId_.clear();
    unlockToBringOnline_ = false;
    emit events_.accountUnlocked(id);
    if (pendingDeleteId_ == id) {
        deleteAccount(id);
        return;
    }
    if (bringOnline) {
        setAccountOffline(id, false);
        return;
    }
    account->goOffline();
}

void CoreHost::cancelUnlock()
{
    unlockingId_.clear();
    unlockToBringOnline_ = false;
    pendingDeleteId_.clear();
    refreshAccounts();
}

void CoreHost::announcePairing()
{
    emit events_.pairing(pairStatus_, pairProgress_, pairNeedsCode_, pairing_);
}

void CoreHost::failPairing(const QString& reason)
{
    pairing_ = false;
    pairNeedsCode_ = false;
    pairCancel_.reset();
    pairEndpoint_.reset();
    pairStatus_ = reason;
    pairProgress_ = kProgressUnknown;
    announcePairing();
    emit events_.pairingFinished(false);
}

void CoreHost::startPairing(const QString& link, const QString& atRestPassphrase)
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
    announcePairing();

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
                    announcePairing();
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
                announcePairing();
            },
            Qt::QueuedConnection);
    }).detach();
}

void CoreHost::submitPairCode(const QString& code)
{
    if (!pairNeedsCode_ || !pairEndpoint_ || !client::isPairCode(code.toStdString())) {
        return;
    }
    pairNeedsCode_ = false;
    pairProgress_ = kProgressUnknown;
    pairStatus_ = tr("Receiving the account");
    announcePairing();

    const std::shared_ptr<bazarish::i2p::Endpoint> endpoint = pairEndpoint_;
    const std::shared_ptr<std::atomic<bool>> cancel = pairCancel_;
    const std::string dest = pairDest_.toStdString();
    const std::string want = code.toStdString();
    const QString atRest = pairAtRest_;
    std::thread([this, endpoint, cancel, dest, want, atRest]() {
        QString failure;
        QString refusedKind;
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
                              announcePairing();
                          },
                          Qt::QueuedConnection);
                  };
            const client::PairFetchResult got
                = client::fetchPairBundle(*endpoint, dest, want, onProgress, *cancel);
            if (got.wrongCode) {
                wrongCode = true;
                triesLeft = got.triesLeft;
            } else if (const bazarish::AccountKind kind = client::Session::bundleKind(got.bundle, want);
                       kind != bazarish::AccountKind::eUser) {
                refusedKind = QString::fromUtf8(bazarish::accountKindName(kind));
            } else {
                id = manager_->import(std::string{}, got.bundle, want, atRest.toStdString()).id;
            }
        } catch (const std::exception& error) {
            failure = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, cancel, failure, refusedKind, wrongCode, triesLeft, id, atRest]() {
                if (pairCancel_ != cancel) {
                    return;
                }
                if (!refusedKind.isEmpty()) {
                    failPairing(tr("This account was created as a bot or a group."));
                    emit events_.importRefused(refusedKind);
                    return;
                }
                if (wrongCode) {
                    pairNeedsCode_ = true;
                    pairStatus_ = tr("Wrong code. %1 tries left").arg(triesLeft);
                    announcePairing();
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
                announcePairing();
                emit events_.pairingFinished(true);
                refreshAccountList();
                openSession(QString::fromStdString(id), atRest, /*makeActive=*/true);
            },
            Qt::QueuedConnection);
    }).detach();
}

void CoreHost::cancelPairing()
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
    announcePairing();
}

void CoreHost::importAccount(const QString& name, const QString& path, const QString& password,
    const QString& atRestPassphrase)
{
    log_.begin(kRestoreOperation, QStringLiteral("account"), tr("Restoring your account"),
        tr("Opening the backup…"));
    std::thread([this, name, path, password, atRestPassphrase]() {
        std::string id;
        QString failure;
        QString refusedKind;
        try {
            const auto sealed = readWhole(path);
            const bazarish::AccountKind kind
                = client::Session::bundleKind(sealed, password.toStdString());
            if (kind != bazarish::AccountKind::eUser) {
                refusedKind = QString::fromUtf8(bazarish::accountKindName(kind));
            } else {
                id = manager_
                         ->import(name.toStdString(), sealed, password.toStdString(),
                             atRestPassphrase.toStdString())
                         .id;
            }
        } catch (const std::exception& error) {
            failure = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, id, failure, refusedKind, atRestPassphrase]() {
                if (!refusedKind.isEmpty()) {
                    log_.finish(kRestoreOperation, false, tr("Not restored."));
                    emit events_.importRefused(refusedKind);
                    return;
                }
                log_.finish(kRestoreOperation, failure.isEmpty(),
                    failure.isEmpty() ? tr("Restored.") : failure);
                if (!failure.isEmpty()) {
                    emit events_.createFailed(failure);
                    return;
                }
                refreshAccountList();
                openSession(QString::fromStdString(id), atRestPassphrase, /*makeActive=*/true);
            },
            Qt::QueuedConnection);
    }).detach();
}

void CoreHost::deleteAccount(const QString& id)
{
    if (!deletingId_.isEmpty()) {
        return;
    }
    setDeleting(id);
    CoreAccount* account = sessionFor(id);
    if (account == nullptr) {
        for (const AccountListRow& info : accountRows_) {
            if (info.id == id && info.encrypted) {
                setDeleting({});
                emit events_.accountDeleteNeedsUnlock(id, info.name);
                return;
            }
        }
        pendingDeleteId_ = id;
        openSession(id, {}, /*makeActive=*/false);
        account = sessionFor(id);
        if (account == nullptr) {
            setDeleting({});
            return;
        }
    }
    pendingDeleteId_.clear();
    if (!account->hasServer()) {
        forgetAccountLocally(id);
        return;
    }
    const auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(&account->events(), &AccountEvents::accountClosed, this,
        [this, id, connection](const bool ok, const QString& error) {
            disconnect(*connection);
            if (!ok) {
                setDeleting({});
                emit events_.accountDeleteFailed(id, error, /*profileNotOpened=*/false);
                return;
            }
            forgetAccountLocally(id);
        });
    emit account->commands().requestCloseAccountOnServer();
}

void CoreHost::deleteAccountAfterUnlock(const QString& id)
{
    pendingDeleteId_ = id;
    openSession(id, {}, /*makeActive=*/false);
    if (sessionFor(id) != nullptr) {
        deleteAccount(id);
    }
}

void CoreHost::forgetAccountLocally(const QString& id)
{
    if (CoreAccount* const account = sessionFor(id)) {
        setDeleting(id);
        pendingRemovals_.insert(id);
        removeSession(account);
        return;
    }
    removeAccountFiles(id);
}

void CoreHost::switchTo(const QString& id)
{
    if (sessionFor(id) != nullptr) {
        setActive(id);
        return;
    }
    openSession(id, {}, /*makeActive=*/true, /*startOnline=*/!offline_.contains(id));
}

void CoreHost::setOnline(const QString& id, const bool on)
{
    CoreAccount* const account = sessionFor(id);
    if (!on) {
        setAccountOffline(id, true);
        if (account != nullptr) {
            account->goOffline();
        }
        refreshAccounts();
        return;
    }
    if (account != nullptr) {
        setAccountOffline(id, false);
        account->goOnline();
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

void CoreHost::setPortable(const bool on)
{
    namespace fs = std::filesystem;
    if (on == client::AccountManager::portable()) {
        return;
    }
    const fs::path from = on ? client::AccountManager::globalRoot()
                             : client::AccountManager::portableRoot();
    const fs::path to = on ? client::AccountManager::portableRoot()
                           : client::AccountManager::globalRoot();
    if (on && underTempDirectory(client::AccountManager::portableRoot())) {
        emit events_.createFailed(tr("The application is running from a temporary directory (%1), which is cleared on reboot. Move it to a folder of its own first.")
                .arg(pathText(client::AccountManager::portableRoot().parent_path())));
        return;
    }
    if (on && !directoryIsWritable(client::AccountManager::portableRoot().parent_path())) {
        emit events_.createFailed(tr("Cannot write beside the application (%1). Move it somewhere writable and try again.")
                .arg(pathText(client::AccountManager::portableRoot().parent_path())));
        return;
    }
    closeAllSessions();
    std::error_code error;
    if (fs::exists(from, error) && !fs::is_empty(from, error)) {
        if (fs::exists(to, error) && !fs::is_empty(to, error)) {
            emit events_.createFailed(tr("There is already data at %1 - move or remove it first, so "
                                         "nothing is overwritten.")
                    .arg(pathText(to)));
            return;
        }
        fs::create_directories(to.parent_path(), error);
        fs::rename(from, to, error);
        if (error) {
            error.clear();
            fs::copy(from, to, fs::copy_options::recursive, error);
            if (error) {
                emit events_.createFailed(tr("Could not move the data: %1")
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
    emit events_.portableChanged();
    emit events_.restartRequired(on
            ? tr("Your data now lives beside the app. Bazarish has to be started "
                             "again to use it.")
            : tr("Your data moved back to your user folder. Bazarish has to be "
                             "started again to use it."));
}

void CoreHost::rebuildI2pLinks()
{
    client::flushWarmDests();
    for (CoreAccount* const account : sessions_) {
        account->rebuildI2pLinks();
    }
}

void CoreHost::setLanguage(const QString& code)
{
    AppSettings::instance().setLanguage(code.toStdString());
    emit languageChosen(code);
}

void CoreHost::setCamera(const QString& id)
{
    AppSettings::instance().setCamera(id.toStdString());
}

void CoreHost::setLightTheme(const bool on)
{
    AppSettings::instance().setLightTheme(on);
}

void CoreHost::setScreenCapture(const bool on)
{
    AppSettings::instance().setScreenCapture(on);
}

void CoreHost::chooseAudio(const QString& input, const QString& output)
{
    AppSettings::instance().setAudioInput(input.toStdString());
    AppSettings::instance().setAudioOutput(output.toStdString());
    AudioRoute::instance().chooseInput(input);
    AudioRoute::instance().chooseOutput(output);
}

void CoreHost::prepareForExit()
{
    if (exiting_) {
        return;
    }
    exiting_ = true;
    emit events_.closing();
    emit readyToExit();
}

void CoreHost::closeAllSessions()
{
    const QList<CoreAccount*> open = sessions_;
    for (CoreAccount* const account : open) {
        removeSession(account);
    }
    refreshAccounts();
}

CoreListener::CoreListener(CoreHost& core, const QString& name, QObject* const parent)
    : QObject(parent)
    , core_(core)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!QLocalServer::removeServer(name)) {
        throw std::runtime_error("a stale core socket could not be removed: " + name.toStdString());
    }
    if (!server_.listen(name)) {
        throw std::runtime_error("the core cannot listen as " + name.toStdString() + ": "
            + server_.errorString().toStdString());
    }
    connect(&server_, &QLocalServer::newConnection, this, &CoreListener::accept);
    bazarish::log::info("wire: the core listens");
}

void CoreListener::dropWindow()
{
    if (SocketChannelEnd* const old = std::exchange(window_, nullptr)) {
        core_.detachWindow();
        old->close();
        old->deleteLater();
    }
}

void CoreListener::flushWindow()
{
    if (window_ != nullptr && !window_->flush()) {
        bazarish::log::warn("wire: the window did not take everything before the exit");
    }
}

void CoreListener::accept()
{
    while (QLocalSocket* const socket = server_.nextPendingConnection()) {
        if (window_ != nullptr) {
            bazarish::log::info("wire: a new window replaces the attached one");
            dropWindow();
        }
        auto* const channel = new SocketChannelEnd(socket, this);
        window_ = channel;
        connect(channel, &SocketChannelEnd::closed, this, [this, channel]() {
            if (window_ != channel) {
                return;
            }
            bazarish::log::info("wire: the window went away");
            core_.detachWindow();
            window_ = nullptr;
            channel->deleteLater();
        });
        core_.attachWindow(*channel);
    }
}

}  // namespace bazarish::app
