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
#include <QWindow>

#include <array>

namespace bazarish::app {

namespace {

constexpr std::array<int, 5> kTraySizes{16, 22, 24, 32, 48};
constexpr int kUnreadTintAlpha = 140;
constexpr int kPopupMs = 6000;
constexpr int kSoundSpacingNumerator = 3;
constexpr int kSoundSpacingDenominator = 2;
constexpr int kAssumedSoundMs = 1000;
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

TrayIcon::TrayIcon(AppController& app, QObject* const parent)
    : QObject(parent)
    , app_(app)
    , sound_(app.soundFolder(), QStringLiteral("notify.wav"))
    , reactionSound_(app.soundFolder(), QStringLiteral("reaction.wav"))
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
    connect(&app_, &AppController::notificationRequested, this, &TrayIcon::notify);
    connect(&app_, &AppController::reactionNotificationRequested, this,
        &TrayIcon::notifyReaction);
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
    if (!hasUnread) {
        announcedWasRead_ = true;
    }
    if (hasUnread != showingUnread_) {
        showingUnread_ = hasUnread;
        tray_.setIcon(hasUnread ? unreadIcon_ : idleIcon_);
    }
    tray_.setToolTip(hasUnread ? tr("Bazarish - %1 unread").arg(unread)
                               : tr("Bazarish"));
}

void TrayIcon::notify(const QString& accountId, const QString& peer, const QString& title,
    const QString& body)
{
    announce(accountId, peer, title, body, sound_, sinceSound_);
}

void TrayIcon::notifyReaction(
    const QString& accountId, const QString& peer, const QString& title, const QString& body)
{
    announce(accountId, peer, title, body, reactionSound_, sinceReactionSound_);
}

void TrayIcon::openNotified()
{
    if (!notifiedAccount_.isEmpty() && !notifiedPeer_.isEmpty()) {
        app_.openConversationOf(notifiedAccount_, notifiedPeer_);
    }
    showWindow();
}

void TrayIcon::announce(const QString& accountId, const QString& peer, const QString& title,
    const QString& body, NotifySound& sound, QElapsedTimer& since)
{
    if (!app_.notificationsEnabled()) {
        return;
    }
    if (QApplication::applicationState() == Qt::ApplicationActive) {
        return;
    }
    if (!app_.ringingPeer().isEmpty()) {
        return;
    }
    notifiedAccount_ = accountId;
    notifiedPeer_ = peer;
    tray_.showMessage(title, body, idleIcon_, kPopupMs);
    const qint64 length = sound.durationMs() > 0 ? sound.durationMs() : kAssumedSoundMs;
    const qint64 spacing = length * kSoundSpacingNumerator / kSoundSpacingDenominator;
    const bool quiet = !announcedWasRead_ && since.isValid() && since.elapsed() < spacing;
    if (quiet) {
        return;
    }
    announcedWasRead_ = false;
    since.start();
    sound.play();
}

}  // namespace bazarish::app
