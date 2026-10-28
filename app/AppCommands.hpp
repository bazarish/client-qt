// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>

namespace bazarish::app {

class AppCommands : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void refreshAccountList();
    void openAccount(const QString& id, const QString& passphrase);
    void switchTo(const QString& id);
    void setOnline(const QString& id, bool on);
    void createAccount(const QString& name, const QString& passphrase);
    void importAccount(const QString& name, const QString& path, const QString& password,
        const QString& atRestPassphrase);
    void startPairing(const QString& link, const QString& atRestPassphrase);
    void submitPairCode(const QString& code);
    void cancelPairing();
    void deleteAccount(const QString& id);
    void deleteAccountAfterUnlock(const QString& id);
    void forgetAccountLocally(const QString& id);
    void cancelUnlock();
    void setNotificationsEnabled(bool on);
    void setBackgroundTasksVisible(bool on);
    void setSpeakerOn(bool on);
    void setPortable(bool on);
    void closeAllSessions();
    void windowActive(bool active);
    void callScreenShown(bool shown);
    void setLanguage(const QString& code);
    void setLightTheme(bool on);
    void setCamera(const QString& id);
    void setScreenCapture(bool on);
    void chooseAudio(const QString& input, const QString& output);
    void quit();
};

}  // namespace bazarish::app
