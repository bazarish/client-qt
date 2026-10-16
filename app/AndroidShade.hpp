// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>

namespace bazarish::app {

class AppController;
class CoreHost;
class Notifier;
class Translations;

class AndroidShade : public QObject {
    Q_OBJECT
public:
    AndroidShade(CoreHost& core, AppController& app, Notifier& notifier,
        Translations& translations, QObject* parent = nullptr);
    ~AndroidShade() override;

    // Android drops a native process's stderr, which is where the app's log goes.
    static void routeStderrToLogcat();
    static bool windowless();
    static bool backgroundRestricted();
    static void openBackgroundSettings();
    static bool fullScreenCallsRestricted();
    static void openFullScreenCallsSettings();
    static void moveToBackground();
    static void startRinging();
    static void dismissKeyguard();
    static void stopRinging();

    void stop();

private:
    void show();
    QString presenceLine() const;
    void notifyMessage(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, bool reaction);
    void followRinging();
    void openHeldConversation();

    CoreHost& core_;
    AppController& app_;
    QString line_;
    bool callShown_ = false;
    bool overLock_ = false;
    bool talking_ = false;
};

}  // namespace bazarish::app
