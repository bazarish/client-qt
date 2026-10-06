// Bazarish project (c) 2026
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bazarish::app {

std::filesystem::path accountsRoot();
std::filesystem::path appRoot();

class AppSettings {
public:
    static AppSettings& instance();

    std::string activeAccount() const;
    void setActiveAccount(const std::string& id);

    std::string language() const;
    void setLanguage(const std::string& code);

    std::vector<std::string> offlineAccounts() const;
    void setOfflineAccounts(std::vector<std::string> ids);

    bool notifications() const;
    void setNotifications(bool on);

    bool backgroundTasks() const;
    void setBackgroundTasks(bool on);

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
    std::string gatewayPin() const;
    void rememberGateway(const std::string& address, const std::string& pin);
    void useGateway(bool on);
    void skipGateway();

    std::string i2pProxyHost() const;
    int i2pProxyPort() const;
    void setI2pProxy(const std::string& host, int port);

private:
    AppSettings();

    void load();
    void save() const;

    std::filesystem::path path_;
    std::string activeAccount_;
    std::string language_;
    std::vector<std::string> offlineAccounts_;
    bool notifications_ = true;
    bool backgroundTasks_ = false;
    bool i2pLogging_ = false;
    int i2pTunnelLength_ = 0;
    std::string i2pProxyHost_;
    int i2pProxyPort_ = 0;
    bool gatewayAsked_ = false;
    bool gatewayEnabled_ = false;
    std::string gatewayAddress_;
    std::string gatewayPin_;
    bool samEnabled_ = false;
    std::string samHost_;
    int samPort_ = 0;
};

}  // namespace bazarish::app
