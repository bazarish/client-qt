// Bazarish project (c) 2026
#include "AppController.hpp"

#include "AccountManager.hpp"
#include "AppSettings.hpp"
#include "AudioRoute.hpp"
#include "Translations.hpp"
#include "Markup.hpp"
#include "PopupStack.hpp"
#include "Version.hpp"
#include "Wire.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidWindow.hpp"
#endif

#include <bazarish/I2p.hpp>
#include <bazarish/Links.hpp>
#include <bazarish/PairLink.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QClipboard>
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QUrl>

#include <exception>
#include <utility>

namespace bazarish::app {

namespace {

const QString kAppScope = QStringLiteral("app");

}  // namespace

AppController::AppController(Translations& translations, QObject* parent)
    : QObject(parent)
    , lightTheme_(AppSettings::instance().lightTheme())
{
    connect(&translations, &Translations::languageChanged, this, [this, &translations]() {
        emit commands_.setLanguage(translations.language());
    });
    for (const auto changed : {&AudioRoute::inputChanged, &AudioRoute::outputChanged}) {
        connect(&AudioRoute::instance(), changed, this, [this]() {
            emit commands_.chooseAudio(
                AudioRoute::instance().inputId(), AudioRoute::instance().outputId());
        });
    }
    // Coming back from the system settings is when the answer may have changed.
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
        &AppController::backgroundRestrictedChanged);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
        [this](const Qt::ApplicationState state) {
            emit commands_.windowActive(state == Qt::ApplicationActive);
        });

    connect(&events_, &AppEvents::started, this, [this]() {
        attached_ = true;
        // The snapshot names the call that rang when the notice was tapped; a later one is not it.
        answerWhenRinging_ = false;
        emit commands_.windowActive(QGuiApplication::applicationState() == Qt::ApplicationActive);
        emit attachedChanged();
    });
    connect(&events_, &AppEvents::closing, this, [this]() { coreClosing_ = true; });
    connect(&events_, &AppEvents::accountList, this, [this](const QVector<AccountListRow>& rows) {
        accountRows_ = rows;
        accountList_.setAccounts(rows);
        emit accountListChanged();
    });
    connect(&events_, &AppEvents::accounts, this, [this](const QVector<AccountRow>& rows) {
        accountStatuses_ = rows;
        accounts_.setAccounts(rows);
        emit accountsChanged();
    });
    connect(&events_, &AppEvents::sessionStarted, this, &AppController::startSession);
    connect(&events_, &AppEvents::sessionEnded, this, &AppController::endSession);
    connect(&events_, &AppEvents::accountReady, this, [this]() {
        emit accountOpened();
        applyPendingLink();
    });
    connect(&events_, &AppEvents::activeChanged, this, [this](const QString& id) {
        activeId_ = id;
        emit sessionChanged();
        emit accountsChanged();
        if (!id.isEmpty()) {
            applyPendingLink();
        }
    });
    connect(&events_, &AppEvents::needPassphrase, this, &AppController::needPassphrase);
    connect(&events_, &AppEvents::unlockFailed, this, &AppController::unlockFailed);
    connect(&events_, &AppEvents::accountUnlocked, this, &AppController::accountUnlocked);
    connect(&events_, &AppEvents::accountOpenFailed, this, &AppController::accountOpenFailed);
    connect(&events_, &AppEvents::createFailed, this, &AppController::createFailed);
    connect(&events_, &AppEvents::accountDeleteFailed, this, &AppController::accountDeleteFailed);
    connect(&events_, &AppEvents::accountDeleteNeedsUnlock, this,
        &AppController::accountDeleteNeedsUnlock);
    connect(&events_, &AppEvents::deleting, this, [this](const QString& id) {
        deletingId_ = id;
        emit deletingChanged();
    });
    connect(&events_, &AppEvents::pairing, this,
        [this](const QString& status, const double progress, const bool needsCode,
            const bool active) {
            pairStatus_ = status;
            pairProgress_ = progress;
            pairNeedsCode_ = needsCode;
            pairing_ = active;
            emit pairingChanged();
        });
    connect(&events_, &AppEvents::pairingFinished, this, &AppController::pairingFinished);
    connect(&events_, &AppEvents::settings, this,
        [this](const bool notifications, const bool backgroundTasks) {
            if (notifications_ != notifications) {
                notifications_ = notifications;
                emit notificationsEnabledChanged();
            }
            if (backgroundTasks_ != backgroundTasks) {
                backgroundTasks_ = backgroundTasks;
                emit backgroundTasksVisibleChanged();
            }
        });
    connect(&events_, &AppEvents::ringing, this,
        [this](const QString& account, const QString& peer, const QString& fingerprint,
            const QString& accountName) {
            ringingAccount_ = account;
            ringingPeer_ = peer;
            ringingPeerFingerprint_ = fingerprint;
            ringingAccountName_ = accountName;
            emit ringingChanged();
            if (answerWhenRinging_ && !account.isEmpty()) {
                answerWhenRinging_ = false;
                answerRinging();
            }
        });
    connect(&events_, &AppEvents::ringLevel, this, [this](const double level) {
        ringLevel_ = level;
        emit ringLevelChanged();
    });
    connect(&events_, &AppEvents::call, this, [this](const bool live) {
        callLive_ = live;
        emit callLiveChanged();
    });
    connect(&events_, &AppEvents::speakerOn, this, [this](const bool on) {
        if (speakerOn_ != on) {
            speakerOn_ = on;
            emit speakerOnChanged();
        }
    });
    connect(&events_, &AppEvents::portableChanged, this, &AppController::portableChanged);
    connect(&events_, &AppEvents::restartRequired, this, &AppController::restartRequired);
    connect(&events_, &AppEvents::opChanged, this, [this](const OperationRow& row) {
        operations_.upsert(row);
        emit operationsChanged();
    });
    connect(&events_, &AppEvents::opRemoved, this, [this](const QString& id) {
        operations_.remove(id);
        emit operationsChanged();
    });
}

void AppController::attach(Relay& relay)
{
    relay_ = &relay;
    relay.sendFrom(kAppScope, commands_);
    relay.deliverTo(kAppScope, events_);
}

void AppController::detach()
{
    const QList<SessionController*> open = sessions_;
    for (SessionController* const ctrl : open) {
        endSession(ctrl->accountId());
    }
    attached_ = false;
    emit attachedChanged();
}

void AppController::startSession(const QString& id)
{
    auto* const ctrl = new SessionController(id, this);
    sessions_.append(ctrl);
    relay_->sendFrom(id, ctrl->commands());
    relay_->deliverTo(id, ctrl->events());
    connect(ctrl, &SessionController::openFailed, this, &AppController::accountOpenFailed);
    connect(ctrl, &SessionController::historyOpened, this, [this, id]() {
        if (pendingConversation_.first == id) {
            const auto [account, peer] = std::exchange(pendingConversation_, {});
            openConversationOf(account, peer);
        }
    });
    emit accountsChanged();
    if (id == activeId_) {
        emit sessionChanged();
    }
}

void AppController::endSession(const QString& id)
{
    SessionController* const ctrl = sessionFor(id);
    if (ctrl == nullptr) {
        return;
    }
    relay_->forget(id);
    sessions_.removeAll(ctrl);
    if (activeId_ == id) {
        activeId_.clear();
        emit sessionChanged();
    }
    ctrl->deleteLater();
    emit accountsChanged();
}

SessionController* AppController::sessionFor(const QString& id) const
{
    for (SessionController* const ctrl : sessions_) {
        if (ctrl->accountId() == id) {
            return ctrl;
        }
    }
    return nullptr;
}

QObject* AppController::session()
{
    return sessionFor(activeId_);
}

std::filesystem::path AppController::accountsFolder()
{
    return accountsRoot();
}

void AppController::setNotificationsEnabled(const bool on)
{
    if (notifications_ == on) {
        return;
    }
    notifications_ = on;
    emit notificationsEnabledChanged();
    emit commands_.setNotificationsEnabled(on);
}

void AppController::setBackgroundTasksVisible(const bool on)
{
    if (backgroundTasks_ == on) {
        return;
    }
    backgroundTasks_ = on;
    emit backgroundTasksVisibleChanged();
    emit commands_.setBackgroundTasksVisible(on);
}

void AppController::setLightTheme(const bool on)
{
    if (lightTheme_ == on) {
        return;
    }
    lightTheme_ = on;
    emit lightThemeChanged();
    emit commands_.setLightTheme(on);
}

void AppController::setSpeakerOn(const bool on)
{
    if (speakerOn_ == on) {
        return;
    }
    speakerOn_ = on;
    emit speakerOnChanged();
    emit commands_.setSpeakerOn(on);
}

void AppController::answerRinging()
{
    SessionController* const ctrl = sessionFor(ringingAccount_);
    if (ctrl == nullptr) {
        answerWhenRinging_ = !attached_;
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

int AppController::unreadElsewhere() const
{
    int total = 0;
    for (const AccountRow& row : accountStatuses_) {
        if (row.open && row.id != activeId_) {
            total += row.unread;
        }
    }
    return total;
}

void AppController::refreshAccountList()
{
    emit commands_.refreshAccountList();
}

void AppController::createAccount(const QString& name, const QString& passphrase)
{
    emit commands_.createAccount(name, passphrase);
}

void AppController::openAccount(const QString& id, const QString& passphrase)
{
    emit commands_.openAccount(id, passphrase);
}

void AppController::cancelUnlock()
{
    emit commands_.cancelUnlock();
}

void AppController::importAccount(const QString& name, const QString& fileUrl,
    const QString& password, const QString& atRestPassphrase)
{
    emit commands_.importAccount(name, QUrl(fileUrl).toLocalFile(), password, atRestPassphrase);
}

void AppController::startPairing(const QString& link, const QString& atRestPassphrase)
{
    emit commands_.startPairing(link, atRestPassphrase);
}

void AppController::submitPairCode(const QString& code)
{
    emit commands_.submitPairCode(code);
}

void AppController::cancelPairing()
{
    emit commands_.cancelPairing();
}

void AppController::prepareForExit()
{
    emit commands_.quit();
}

void AppController::closeAllSessions()
{
    emit commands_.closeAllSessions();
}

void AppController::deleteAccount(const QString& id)
{
    emit commands_.deleteAccount(id);
}

void AppController::deleteAccountAfterUnlock(const QString& id)
{
    emit commands_.deleteAccountAfterUnlock(id);
}

void AppController::forgetAccountLocally(const QString& id)
{
    emit commands_.forgetAccountLocally(id);
}

void AppController::switchTo(const QString& id)
{
    emit commands_.switchTo(id);
}

void AppController::openConversationOf(const QString& accountId, const QString& peer)
{
    SessionController* const ctrl = sessionFor(accountId);
    if (ctrl == nullptr || !ctrl->historyIsOpen()) {
        pendingConversation_ = {accountId, peer};
        return;
    }
    pendingConversation_ = {};
    switchTo(accountId);
    ctrl->openConversation(peer);
}

void AppController::setOnline(const QString& id, const bool on)
{
    emit commands_.setOnline(id, on);
}

void AppController::setPortable(const bool on)
{
    emit commands_.setPortable(on);
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
    const bool sign = uri.startsWith(QLatin1String(bazarish::kSignUri));
    if (!invite && !sign && !uri.startsWith(QLatin1String(bazarish::kServerUri))) {
        bazarish::log::warn("asked to open a link this client does not know");
        emit linkRefused(tr("This link is not one Bazarish knows."));
        return;
    }
    emit raiseRequested();
    // These act on an open account, so a link that arrives before one is
    // open waits for it rather than going nowhere.
    if (sessions_.isEmpty()) {
        pendingLink_ = uri;
        return;
    }
    if (invite) {
        emit inviteLinkOpened(uri);
        return;
    }
    if (sign) {
        emit signLinkOpened(uri);
        return;
    }
    emit serverLinkOpened(uri);
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

bool AppController::portable() const
{
    return client::AccountManager::portable();
}

bool AppController::backgroundRestricted() const
{
#ifdef Q_OS_ANDROID
    return AndroidWindow::backgroundRestricted();
#else
    return false;
#endif
}

void AppController::openBackgroundSettings() const
{
#ifdef Q_OS_ANDROID
    AndroidWindow::openBackgroundSettings();
#endif
}

bool AppController::fullScreenCallsRestricted() const
{
#ifdef Q_OS_ANDROID
    return AndroidWindow::fullScreenCallsRestricted();
#else
    return false;
#endif
}

void AppController::openFullScreenCallsSettings() const
{
#ifdef Q_OS_ANDROID
    AndroidWindow::openFullScreenCallsSettings();
#endif
}

void AppController::requestUnlocked()
{
#ifdef Q_OS_ANDROID
    AndroidWindow::dismissKeyguard();
#else
    emit unlocked();
#endif
}

bool AppController::closeTopPopup(QQuickWindow* const window) const
{
    return bazarish::app::closeTopPopup(window);
}

void AppController::paintSystemBars(const QColor& color) const
{
#ifdef Q_OS_ANDROID
    AndroidWindow::paintSystemBars(lightTheme_, color);
#else
    Q_UNUSED(color);
#endif
}

void AppController::moveToBackground() const
{
#ifdef Q_OS_ANDROID
    AndroidWindow::moveToBackground();
#endif
}

QString AppController::dataLocation() const
{
    return QString::fromStdString(client::AccountManager::dataRoot().string());
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

void AppController::requestAddAccount()
{
    emit showPicker();
}

}  // namespace bazarish::app
