// Bazarish project (c) 2026
#pragma once

#include <QColor>
#include <QObject>

#include <optional>

namespace bazarish::app {

class AppController;

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
    static bool canPickPictures();
    static void pickPictures();
    static void pickAvatar();
    static void moveToBackground();
    static void dismissKeyguard();
    static void paintSystemBars(bool light, const QColor& color);
    static void allowScreenCapture(bool on);
    static int gestureEdgeLeft();
    static int gestureEdgeRight();

signals:
    void coreListening();

private:
    void followCall();
    void takeHeld();

    AppController& app_;
    std::optional<bool> overLock_;
};

}  // namespace bazarish::app
