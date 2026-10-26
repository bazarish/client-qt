// Bazarish project (c) 2026
#include "TrayIcon.hpp"

#include "AppController.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QAction>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QWindow>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
#include <QDBusConnection>
#include <QDBusMessage>
#endif

#include <algorithm>
#include <array>

namespace bazarish::app {

namespace {

constexpr std::array<int, 5> kTraySizes{16, 22, 24, 32, 48};
constexpr int kUnreadTintAlpha = 140;
constexpr int kPopupMs = 6000;
constexpr int kRecentlyActiveMs = 400;

QIcon iconFromMaster(const QImage& master, const QColor& tint)
{
    QIcon icon;
    for (const int size : kTraySizes) {
        QPixmap pixmap = QPixmap::fromImage(
            master.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (tint.alpha() > 0) {
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            painter.fillRect(pixmap.rect(), tint);
        }
        icon.addPixmap(pixmap);
    }
    return icon;
}

QString statusOf(const AccountRow& account)
{
    if (!account.open || !account.online) {
        return QObject::tr("disabled");
    }
    return account.connected ? QObject::tr("connected") : QObject::tr("connecting");
}

QString menuLine(const AccountRow& account)
{
    const QString unread
        = account.unread > 0 ? QStringLiteral(" - %1 unread").arg(account.unread) : QString();
    return QStringLiteral("%1: %2%3").arg(account.name, statusOf(account), unread);
}

}  // namespace

bool TrayIcon::available()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

TrayIcon::TrayIcon(AppController& app, QObject* const parent)
    : QObject(parent)
    , app_(app)
{
    const QImage master(QStringLiteral(":/icon/bazarish.png"));
    idleIcon_ = iconFromMaster(master, QColor(Qt::transparent));
    unreadIcon_ = iconFromMaster(master, QColor(0x39, 0xff, 0x14, kUnreadTintAlpha));
    tray_.setIcon(idleIcon_);
    tray_.setContextMenu(&menu_);
    buildMenu();
    refreshMenu();
    refreshIcon();
    tray_.show();
    bazarish::log::info("tray: icon shown, popups {}",
        QSystemTrayIcon::supportsMessages() ? "supported" : "not supported by this desktop");

    connect(&tray_, &QSystemTrayIcon::activated, this,
        [this](const QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                toggleWindow();
            }
        });
    connect(&tray_, &QSystemTrayIcon::messageClicked, this, &TrayIcon::openNotified);
    connect(&app_, &AppController::accountsChanged, this, &TrayIcon::refreshIcon);
    // A panel draws the exported menu it last read: rebuilt as it opens, it comes out collapsed.
    connect(&app_, &AppController::accountsChanged, this, &TrayIcon::refreshMenu);
    connect(&app_, &AppController::notificationsEnabledChanged, this,
        [this]() { notificationsAction_->setChecked(app_.notificationsEnabled()); });
}

void TrayIcon::attachWindow(QWindow* const window)
{
    window_ = window;
    if (window_ == nullptr) {
        return;
    }
    connect(window_, &QWindow::activeChanged, this, [this]() {
        if (window_->isActive()) {
            sinceInactive_.invalidate();
        } else {
            sinceInactive_.start();
        }
    });
}

void TrayIcon::showWindow()
{
    if (window_ == nullptr) {
        return;
    }
    window_->setWindowStates(window_->windowStates() & ~Qt::WindowMinimized);
    window_->show();
    window_->raise();
    window_->requestActivate();
}

void TrayIcon::toggleWindow()
{
    if (window_ == nullptr) {
        return;
    }
    const bool wasActive = window_->isActive()
        || (sinceInactive_.isValid() && sinceInactive_.elapsed() < kRecentlyActiveMs);
    const bool inFront = window_->isVisible()
        && (window_->windowStates() & Qt::WindowMinimized) == 0 && wasActive;
    if (inFront) {
        window_->hide();
        return;
    }
    showWindow();
}

void TrayIcon::buildMenu()
{
    accountsEnd_ = menu_.addSeparator();
    notificationsAction_ = menu_.addAction(tr("Notifications"));
    notificationsAction_->setCheckable(true);
    notificationsAction_->setChecked(app_.notificationsEnabled());
    connect(notificationsAction_, &QAction::toggled, this,
        [this](const bool on) { app_.setNotificationsEnabled(on); });

    menu_.addSeparator();
    QAction* const quit = menu_.addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);
}

void TrayIcon::refreshMenu()
{
    const QVector<AccountRow> accounts = app_.accountStatuses();
    const bool sameAccounts = std::equal(accounts.begin(), accounts.end(),
        accountActions_.begin(), accountActions_.end(),
        [](const AccountRow& account, const QAction* const action) {
            return action->data().toString() == account.id;
        });
    if (!sameAccounts) {
        qDeleteAll(accountActions_);
        accountActions_.clear();
        for (const AccountRow& account : accounts) {
            auto* const action = new QAction(&menu_);
            const QString id = account.id;
            action->setData(id);
            connect(action, &QAction::triggered, this, [this, id]() {
                app_.switchTo(id);
                showWindow();
            });
            menu_.insertAction(accountsEnd_, action);
            accountActions_.append(action);
        }
        accountsEnd_->setVisible(!accounts.isEmpty());
    }
    for (qsizetype i = 0; i < accounts.size(); ++i) {
        accountActions_[i]->setText(menuLine(accounts[i]));
    }
}

void TrayIcon::refreshIcon()
{
    int unread = 0;
    for (const AccountRow& account : app_.accountStatuses()) {
        unread += account.unread;
    }
    const bool hasUnread = unread > 0;
    if (hasUnread != showingUnread_) {
        showingUnread_ = hasUnread;
        tray_.setIcon(hasUnread ? unreadIcon_ : idleIcon_);
    }
    tray_.setToolTip(hasUnread ? tr("Bazarish - %1 unread").arg(unread)
                               : tr("Bazarish"));
}

void TrayIcon::openNotified()
{
    if (!notifiedAccount_.isEmpty() && !notifiedPeer_.isEmpty()) {
        app_.openConversationOf(notifiedAccount_, notifiedPeer_);
    }
    showWindow();
}

void TrayIcon::showNotice(const QString& accountId, const QString& peer, const QString& title,
    const QString& body)
{
    notifiedAccount_ = accountId;
    notifiedPeer_ = peer;
    tray_.showMessage(title, bodyReadsMarkup() ? body.toHtmlEscaped() : body, idleIcon_, kPopupMs);
}

// A notification server that reads markup in the body would read it in a correspondent's words.
bool TrayIcon::bodyReadsMarkup()
{
    if (!bodyReadsMarkup_.has_value()) {
#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
        const QDBusMessage reply = QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("GetCapabilities")));
        bodyReadsMarkup_ = reply.type() == QDBusMessage::ReplyMessage
            && !reply.arguments().isEmpty()
            && reply.arguments().constFirst().toStringList().contains(
                QStringLiteral("body-markup"));
#else
        bodyReadsMarkup_ = false;
#endif
    }
    return *bodyReadsMarkup_;
}

}  // namespace bazarish::app
