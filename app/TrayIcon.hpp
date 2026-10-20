// Bazarish project (c) 2026
#pragma once

#include <QElapsedTimer>
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

#include <optional>

class QWindow;

namespace bazarish::app {

class AppController;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(AppController& app, QObject* parent = nullptr);

    static bool available();

    void attachWindow(QWindow* window);
    void showNotice(const QString& accountId, const QString& peer, const QString& title,
        const QString& body);

signals:
    void quitRequested();

private:
    void rebuildMenu();
    void refreshIcon();
    void openNotified();
    void showWindow();
    void toggleWindow();
    bool bodyReadsMarkup();

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
    std::optional<bool> bodyReadsMarkup_;
};

}  // namespace bazarish::app
