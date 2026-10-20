// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>

namespace bazarish::app {

class CoreHost;
class Notifier;
class Translations;

// The core's notices on Android: the shade line, messages, the ring and the call.
class AndroidShade : public QObject {
    Q_OBJECT
public:
    AndroidShade(CoreHost& core, Notifier& notifier, Translations& translations,
        QObject* parent = nullptr);
    ~AndroidShade() override;

    // Android drops a native process's stderr, which is where the app's log goes.
    static void routeStderrToLogcat();
    static void startRinging();
    static void stopRinging();
    static void listening();
    static void buzzForMessage();

    void stop();

private:
    void show();
    QString presenceLine() const;
    void notifyMessage(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, bool reaction);
    void followRinging();

    CoreHost& core_;
    QString line_;
    bool callShown_ = false;
    bool talking_ = false;
};

}  // namespace bazarish::app
