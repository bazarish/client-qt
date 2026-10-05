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

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(AppController& app, QObject* parent = nullptr);

    static bool available();

    void attachWindow(QWindow* window);

signals:
    void quitRequested();

private:
    void rebuildMenu();
    void refreshIcon();
    void notify(const QString& accountId, const QString& peer, const QString& title,
        const QString& body);
    void notifyReaction(const QString& accountId, const QString& peer, const QString& title,
        const QString& body);
    void announce(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, NotifySound& sound, QElapsedTimer& since);
    void openNotified();
    void showWindow();
    void toggleWindow();

    AppController& app_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* notificationsAction_ = nullptr;
    NotifySound sound_;
    NotifySound reactionSound_;
    QElapsedTimer sinceReactionSound_;
    QString notifiedAccount_;
    QString notifiedPeer_;
    QElapsedTimer sinceSound_;
    bool announcedWasRead_ = true;
    QIcon idleIcon_;
    QIcon unreadIcon_;
    bool showingUnread_ = false;
    QWindow* window_ = nullptr;
    QElapsedTimer sinceInactive_;
};

}  // namespace bazarish::app
