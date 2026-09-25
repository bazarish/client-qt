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

    // Whether the background-activity panel is on screen at all. Off by default:
    // it is a window into what the client is doing, not something every user
    // needs beside their conversations.
    bool backgroundTasks() const;
    void setBackgroundTasks(bool on);

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
    // Whether the question has been put at all, and what came of it. The three
    // states are not two: a user who has not been asked is not a user who said
    // no, and only the first gets a dialog.
    bool gatewayAsked() const;
    bool gatewayEnabled() const;
    std::string gatewayAddress() const;
    std::string gatewayPin() const;
    // Remembering where a gateway is, and the key it presented, without saying
    // that this is what carries the traffic: an address is checked and kept long
    // before anybody chooses to use it, and a check that switched the transport
    // by itself is a check that decided something it was not asked about.
    void rememberGateway(const std::string& address, const std::string& pin);
    // Which engine carries I2P. One of them, always: turning one on turns the
    // other off here rather than in a caller.
    void useGateway(bool on);
    // The user said no. The answer is recorded so it is not asked again, and
    // the embedded router or SAM takes over as it always did.
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
