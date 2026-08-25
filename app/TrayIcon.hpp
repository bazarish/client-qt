// Bazarish project (c) 2026
#pragma once

#include "NotifySound.hpp"

#include <QElapsedTimer>
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class QWindow;

namespace bazarish::app {

class AppController;

// The tray presence: what the application looks like when its window is not on
// screen. It carries the accounts and what each of them is doing, turns popup
// notifications on and off, quits, and says - by its own colour - that something
// is waiting to be read.
class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(AppController& app, QObject* parent = nullptr);

    // False where the desktop offers no tray at all; nothing is shown then and the
    // window is the only way in.
    static bool available();

    // The window the tray shows, hides and brings back.
    void attachWindow(QWindow* window);

signals:
    void quitRequested();

private:
    void rebuildMenu();
    void refreshIcon();
    void notify(const QString& title, const QString& body);
    // Brings the window back the way it was left.
    void showWindow();
    // The tray icon itself: away if the window is in front, back if it is not.
    void toggleWindow();

    AppController& app_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* notificationsAction_ = nullptr;
    NotifySound sound_;
    QIcon idleIcon_;
    QIcon unreadIcon_;
    // What the icon is currently showing, so it is only replaced when the answer
    // changes rather than on every unread count that moves.
    bool showingUnread_ = false;
    QWindow* window_ = nullptr;
    // Running since the window stopped being the active one, invalid while it is.
    // Clicking a panel can take the keyboard focus with it, so "was in front a
    // moment ago" is what a tray click has to go by.
    QElapsedTimer sinceInactive_;
};

}  // namespace bazarish::app
