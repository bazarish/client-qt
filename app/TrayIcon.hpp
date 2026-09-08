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
    void notify(const QString& accountId, const QString& peer, const QString& title,
        const QString& body);
    // A reaction on one of our messages: the same popup, its own shorter sound.
    void notifyReaction(const QString& accountId, const QString& peer, const QString& title,
        const QString& body);
    // Shows the popup and, if it is not being rationed, makes `sound` - the half
    // both notifications share.
    void announce(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, NotifySound& sound, QElapsedTimer& since);
    // Opens what the last notification was about: its account, then its chat.
    void openNotified();
    // Brings the window back the way it was left.
    void showWindow();
    // The tray icon itself: away if the window is in front, back if it is not.
    void toggleWindow();

    AppController& app_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* notificationsAction_ = nullptr;
    NotifySound sound_;
    // A reaction is announced with a shorter, quieter sound of its own, and is
    // rationed apart from messages: a flurry of hearts must not swallow the one
    // sound that says somebody wrote something.
    NotifySound reactionSound_;
    QElapsedTimer sinceReactionSound_;
    // What the last popup was about, so clicking it opens that conversation
    // rather than only bringing the window back.
    QString notifiedAccount_;
    QString notifiedPeer_;
    // The sound is held back while what it announced has not been looked at: a
    // conversation that arrives in twenty messages should not be twenty sounds.
    // It is the sound that waits, never the popup - see notify().
    QElapsedTimer sinceSound_;
    // True once everything that was announced has been read, which lets the next
    // arrival sound at once instead of waiting out the interval.
    bool announcedWasRead_ = true;
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
