// Bazarish project (c) 2026
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bazarish::app {

// Where this installation keeps its accounts. Overridden by
// BAZARISH_ACCOUNTS_DIR, which is how a second copy runs beside the first.
std::filesystem::path accountsRoot();
// The directory the whole installation lives in: the accounts directory, the
// embedded router's state and the settings below are all under it.
std::filesystem::path appRoot();

// The application's own settings: one JSON document at the root of the
// installation, covering everything that belongs to the installation rather than
// to an account. It sits beside the accounts directory rather than inside it,
// because an account directory holds accounts. Read once at start and rewritten whole on every change, so the file
// on disk is always a complete document rather than a set of fragments that can
// disagree with one another.
//
// A missing file means defaults. An unreadable one is reported and then treated
// as missing: settings that cannot be read must not stop the application, but a
// privacy setting silently reverting to its default is not something to pass
// over in silence.
class AppSettings {
public:
    // The one instance; the file it holds is the installation's, not a session's.
    static AppSettings& instance();

    // Which account was in the foreground when the application last closed.
    std::string activeAccount() const;
    void setActiveAccount(const std::string& id);

    // Accounts the user switched off. They are not opened at the next start.
    std::vector<std::string> offlineAccounts() const;
    void setOfflineAccounts(std::vector<std::string> ids);

    // Refuse every clearnet facade, so all traffic goes over I2P only.

    // Desktop notifications for incoming messages and calls.
    bool notifications() const;
    void setNotifications(bool on);

    // Embedded I2P router. The tunnel-length level is the index of a profile
    // (0 minimal, 1 middle, 2 maximum); the proxy is empty when none is set.
    bool i2pLogging() const;
    void setI2pLogging(bool on);
    int i2pTunnelLength() const;
    void setI2pTunnelLength(int level);

    // The transport: the engine in this process, or a router outside it reached
    // over SAM. Off unless the user turns it on - which router carries the
    // traffic is not something to decide for them.
    bool samEnabled() const;
    std::string samHost() const;
    int samPort() const;
    void setSam(bool enabled, const std::string& host, int port);
    std::string i2pProxyHost() const;
    int i2pProxyPort() const;
    void setI2pProxy(const std::string& host, int port);

private:
    AppSettings();

    void load();
    void save() const;

    std::filesystem::path path_;
    std::string activeAccount_;
    std::vector<std::string> offlineAccounts_;
    bool notifications_ = true;
    bool i2pLogging_ = false;
    int i2pTunnelLength_ = 0;
    std::string i2pProxyHost_;
    int i2pProxyPort_ = 0;
    bool samEnabled_ = false;
    std::string samHost_;
    int samPort_ = 0;
};

}  // namespace bazarish::app
