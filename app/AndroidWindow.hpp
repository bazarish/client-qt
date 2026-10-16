// Bazarish project (c) 2026
#pragma once

#include <QObject>

namespace bazarish::app {

class AppController;

// What only the window's process can do on Android: the activity, the lock and the core's start.
class AndroidWindow : public QObject {
    Q_OBJECT
public:
    explicit AndroidWindow(AppController& app, QObject* parent = nullptr);
    ~AndroidWindow() override;

    static void startCore();
    static bool backgroundRestricted();
    static void openBackgroundSettings();
    static bool fullScreenCallsRestricted();
    static void openFullScreenCallsSettings();
    static void moveToBackground();
    static void dismissKeyguard();

signals:
    void coreListening();

private:
    void followCall();
    void takeHeld();

    AppController& app_;
    bool overLock_ = false;
};

}  // namespace bazarish::app
