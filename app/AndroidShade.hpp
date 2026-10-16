// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>

namespace bazarish::app {

class AppController;
class Notifier;
class Translations;

class AndroidShade : public QObject {
    Q_OBJECT
public:
    AndroidShade(AppController& app, Notifier& notifier, Translations& translations,
        QObject* parent = nullptr);
    ~AndroidShade() override;

    static bool backgroundRestricted();
    static void openBackgroundSettings();
    static void moveToBackground();

    void stop();

private:
    void show();
    void notifyMessage(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, bool reaction);
    void followRinging();

    AppController& app_;
    bool callShown_ = false;
};

}  // namespace bazarish::app
