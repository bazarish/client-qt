// Bazarish project (c) 2026
#pragma once

#include <QElapsedTimer>
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class QWindow;

namespace bazarish::app {

class AppController;
class Notifier;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    TrayIcon(AppController& app, Notifier& notifier, QObject* parent = nullptr);

    static bool available();

    void attachWindow(QWindow* window);

signals:
    void quitRequested();

private:
    void rebuildMenu();
    void refreshIcon();
    void showNotice(const QString& accountId, const QString& peer, const QString& title,
        const QString& body);
    void openNotified();
    void showWindow();
    void toggleWindow();

    AppController& app_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* notificationsAction_ = nullptr;
    QString notifiedAccount_;
    QString notifiedPeer_;
    QIcon idleIcon_;
    QIcon unreadIcon_;
    bool showingUnread_ = false;
    QWindow* window_ = nullptr;
    QElapsedTimer sinceInactive_;
};

}  // namespace bazarish::app
