// Bazarish project (c) 2026
#include "TrayIcon.hpp"

#include "AppController.hpp"
#include "Notifier.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QAction>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QWindow>

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
    return account.connected ? QObject::tr("online") : QObject::tr("connecting");
}

}  // namespace

bool TrayIcon::available()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

TrayIcon::TrayIcon(AppController& app, Notifier& notifier, QObject* const parent)
    : QObject(parent)
    , app_(app)
{
    const QImage master(QStringLiteral(":/icon/bazarish.png"));
    idleIcon_ = iconFromMaster(master, QColor(Qt::transparent));
    unreadIcon_ = iconFromMaster(master, QColor(0x39, 0xff, 0x14, kUnreadTintAlpha));
    tray_.setIcon(idleIcon_);
    tray_.setContextMenu(&menu_);
    connect(&menu_, &QMenu::aboutToShow, this, &TrayIcon::rebuildMenu);
    rebuildMenu();
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
    connect(&notifier, &Notifier::announced, this, &TrayIcon::showNotice);
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

void TrayIcon::rebuildMenu()
{
    menu_.clear();
    notificationsAction_ = nullptr;

    const QVector<AccountRow> accounts = app_.accountStatuses();
    for (const AccountRow& account : accounts) {
        const QString unread
            = account.unread > 0 ? QStringLiteral(" - %1 unread").arg(account.unread) : QString();
        QAction* const action = menu_.addAction(
            QStringLiteral("%1: %2%3").arg(account.name, statusOf(account), unread));
        const QString id = account.id;
        connect(action, &QAction::triggered, this, [this, id]() {
            app_.switchTo(id);
            showWindow();
        });
    }
    if (!accounts.isEmpty()) {
        menu_.addSeparator();
    }

    notificationsAction_ = menu_.addAction(tr("Notifications"));
    notificationsAction_->setCheckable(true);
    notificationsAction_->setChecked(app_.notificationsEnabled());
    connect(notificationsAction_, &QAction::toggled, this,
        [this](const bool on) { app_.setNotificationsEnabled(on); });

    menu_.addSeparator();
    QAction* const quit = menu_.addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);
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
    tray_.showMessage(title, body, idleIcon_, kPopupMs);
}

}  // namespace bazarish::app
