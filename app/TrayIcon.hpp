// Bazarish project (c) 2026
#pragma once

#include "NotifySound.hpp"

#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

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

signals:
    void showWindowRequested();
    void quitRequested();

private:
    void rebuildMenu();
    void refreshIcon();
    void notify(const QString& title, const QString& body);

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
};

}  // namespace bazarish::app
