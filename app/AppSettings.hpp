// Bazarish project (c) 2026
#pragma once

#include "UiLock.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bazarish::app {

std::filesystem::path accountsRoot();
std::filesystem::path appRoot();
std::filesystem::path i2pRoot();

class AppSettings {
public:
    static AppSettings& instance();

    // Why the settings file could not be read; empty when it was read or is not there yet.
    std::string unreadable() const;
    const std::filesystem::path& path() const;

    std::string activeAccount() const;
    void setActiveAccount(const std::string& id);

    std::string language() const;
    void setLanguage(const std::string& code);

    bool lightTheme() const;
    void setLightTheme(bool on);

    std::string camera() const;
    void setCamera(const std::string& id);

    bool screenCapture() const;
    void setScreenCapture(bool on);

    std::vector<std::string> offlineAccounts() const;
    void setOfflineAccounts(std::vector<std::string> ids);

    bool notifications() const;
    void setNotifications(bool on);

    bool notificationText() const;
    void setNotificationText(bool on);

    bool systemNotifications() const;
    void setSystemNotifications(bool on);

    bool backgroundTasks() const;
    void setBackgroundTasks(bool on);

    std::string audioInput() const;
    void setAudioInput(const std::string& id);
    std::string audioOutput() const;
    void setAudioOutput(const std::string& id);

    bool i2pLogging() const;
    void setI2pLogging(bool on);
    int i2pTunnelLength() const;
    void setI2pTunnelLength(int level);

    bool samEnabled() const;
    std::string samHost() const;
    int samPort() const;
    void setSam(bool enabled, const std::string& host, int port);
    bool gatewayAsked() const;
    bool gatewayEnabled() const;
    std::string gatewayAddress() const;
    void rememberGateway(const std::string& address);
    void useGateway(bool on);
    void skipGateway();

    std::string i2pProxyHost() const;
    int i2pProxyPort() const;
    void setI2pProxy(const std::string& host, int port);

    const std::optional<UiLockPin>& uiLockPin() const { return uiLockPin_; }
    void setUiLockPin(std::optional<UiLockPin> pin);
    int uiAutoLockSeconds() const { return uiAutoLockSeconds_; }
    void setUiAutoLockSeconds(int seconds);
    bool uiLockFingerprint() const { return uiLockFingerprint_; }
    void setUiLockFingerprint(bool on);
    bool uiLockHidesAll() const { return uiLockHidesAll_; }
    void setUiLockHidesAll(bool on);
    bool uiLockPhoto() const { return uiLockPhoto_; }
    void setUiLockPhoto(bool on);

private:
    AppSettings();

    void load();
    void save() const;

    std::filesystem::path path_;
    std::string unreadable_;
    std::string activeAccount_;
    std::string language_;
    bool lightTheme_ = true;
    std::string camera_;
    bool screenCapture_ = false;
    std::vector<std::string> offlineAccounts_;
    bool notifications_ = true;
    bool notificationText_ = true;
    bool systemNotifications_ = false;
    bool backgroundTasks_ = false;
    std::string audioInput_;
    std::string audioOutput_;
    bool i2pLogging_ = false;
    int i2pTunnelLength_ = 0;
    std::string i2pProxyHost_;
    int i2pProxyPort_ = 0;
    bool gatewayAsked_ = false;
    bool gatewayEnabled_ = false;
    std::string gatewayAddress_;
    bool samEnabled_ = false;
    std::string samHost_;
    int samPort_ = 0;
    std::optional<UiLockPin> uiLockPin_;
    int uiAutoLockSeconds_ = 0;
    bool uiLockFingerprint_ = true;
    bool uiLockHidesAll_ = false;
    bool uiLockPhoto_ = true;
};

}  // namespace bazarish::app
