// Bazarish project (c) 2026
#include "TrayIcon.hpp"

#include "AppController.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>

#include <array>

namespace bazarish::app {

namespace {

// The sizes a tray asks for. The master icon is 512x512, and a tray handed that
// alone has nothing to draw at the size it actually wants.
constexpr std::array<int, 5> kTraySizes{16, 22, 24, 32, 48};
// Unread turns the icon the brand's own green (#39ff14), washed over the tile
// rather than replacing it: the mark stays readable, and the colour change is
// visible at 22 pixels on a light panel and a dark one alike.
constexpr int kUnreadTintAlpha = 140;
// How long a popup stays up. Long enough to read a name, short enough not to sit
// over other work.
constexpr int kPopupMs = 6000;

QIcon iconFromMaster(const QImage& master, const QColor& tint)
{
    QIcon icon;
    for (const int size : kTraySizes) {
        QPixmap pixmap = QPixmap::fromImage(
            master.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (tint.alpha() > 0) {
            QPainter painter(&pixmap);
            // Only where the icon already is: the tile takes the colour and the
            // transparent corners stay transparent.
            painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            painter.fillRect(pixmap.rect(), tint);
        }
        icon.addPixmap(pixmap);
    }
    return icon;
}

QString statusOf(const AccountRow& account)
{
    // An account that is not open is not running either, which is the same thing
    // to a user reading a list of them.
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

TrayIcon::TrayIcon(AppController& app, QObject* const parent)
    : QObject(parent)
    , app_(app)
    , sound_(app.soundFolder())
{
    const QImage master(QStringLiteral(":/icon/bazarish.png"));
    idleIcon_ = iconFromMaster(master, QColor(Qt::transparent));
    unreadIcon_ = iconFromMaster(master, QColor(0x39, 0xff, 0x14, kUnreadTintAlpha));
    tray_.setIcon(idleIcon_);
    tray_.setContextMenu(&menu_);
    // Built when it is about to be shown rather than on every change: the accounts
    // republish their status on each sync, and a menu rebuilt under the pointer is
    // a menu that closes itself while being read.
    connect(&menu_, &QMenu::aboutToShow, this, &TrayIcon::rebuildMenu);
    rebuildMenu();
    refreshIcon();
    tray_.show();
    bazarish::log::info("tray: icon shown, popups {}",
        QSystemTrayIcon::supportsMessages() ? "supported" : "not supported by this desktop");

    connect(&tray_, &QSystemTrayIcon::activated, this,
        [this](const QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                emit showWindowRequested();
            }
        });
    connect(&tray_, &QSystemTrayIcon::messageClicked, this, &TrayIcon::showWindowRequested);
    connect(&app_, &AppController::accountsChanged, this, &TrayIcon::refreshIcon);
    connect(&app_, &AppController::notificationRequested, this, &TrayIcon::notify);
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
        action->setCheckable(true);
        action->setChecked(account.active);
        const QString id = account.id;
        connect(action, &QAction::triggered, this, [this, id]() {
            app_.switchTo(id);
            emit showWindowRequested();
        });
    }
    if (!accounts.isEmpty()) {
        menu_.addSeparator();
    }

    notificationsAction_ = menu_.addAction(tr("Show notifications"));
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
    tray_.setToolTip(hasUnread ? tr("Bazarish - %n unread message(s)", nullptr, unread)
                               : tr("Bazarish"));
}

void TrayIcon::notify(const QString& title, const QString& body)
{
    if (!app_.notificationsEnabled()) {
        return;
    }
    // Nothing is announced while the user is looking at the application: they can
    // already see it happen.
    if (QApplication::applicationState() == Qt::ApplicationActive) {
        return;
    }
    tray_.showMessage(title, body, idleIcon_, kPopupMs);
    sound_.play();
}

}  // namespace bazarish::app
