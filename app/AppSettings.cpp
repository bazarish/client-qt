// Bazarish project (c) 2026
#include "AppSettings.hpp"

#include "AccountManager.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <utility>

namespace bazarish::app {

namespace {

// The lowest tunnel-length level, and the count of them. Kept here rather than
// taken from the I2P layer: this file stores a number, and what it selects is
// the caller's business.
constexpr int kMinTunnelLength = 0;
constexpr int kMaxTunnelLength = 2;

}  // namespace

std::filesystem::path accountsRoot()
{
    if (const char* const env = std::getenv("BAZARISH_ACCOUNTS_DIR");
        env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return client::AccountManager::defaultRoot();
}

std::filesystem::path appRoot()
{
    return accountsRoot().parent_path();
}

AppSettings& AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
    : path_(appRoot() / "settings.json")
{
    load();
}

void AppSettings::load()
{
    std::ifstream in(path_);
    if (!in.good()) {
        return;  // no file yet: the defaults above are the settings
    }
    try {
        nlohmann::json document;
        in >> document;
        activeAccount_ = document.value("activeAccount", std::string());
        offlineAccounts_ = document.value("offlineAccounts", std::vector<std::string>());
        fullPrivacy_ = document.value("fullPrivacyMode", false);
        notifications_ = document.value("notifications", true);
        const nlohmann::json i2p = document.value("i2p", nlohmann::json::object());
        i2pLogging_ = i2p.value("logging", false);
        i2pTunnelLength_
            = std::clamp(i2p.value("tunnelLength", kMinTunnelLength), kMinTunnelLength,
                kMaxTunnelLength);
        const nlohmann::json proxy = i2p.value("proxy", nlohmann::json::object());
        i2pProxyHost_ = proxy.value("host", std::string());
        i2pProxyPort_ = proxy.value("port", 0);
        if (i2pProxyHost_.empty() || i2pProxyPort_ <= 0) {
            i2pProxyHost_.clear();
            i2pProxyPort_ = 0;
        }
    } catch (const std::exception& error) {
        // Defaults from here, which is survivable - but a privacy setting that
        // has quietly reverted must be visible somewhere.
        bazarish::log::warn("settings not read, using defaults: {}", error.what());
    }
}

void AppSettings::save() const
{
    const nlohmann::json document = {
        {"activeAccount", activeAccount_},
        {"offlineAccounts", offlineAccounts_},
        {"fullPrivacyMode", fullPrivacy_},
        {"notifications", notifications_},
        {"i2p",
            {
                {"logging", i2pLogging_},
                {"tunnelLength", i2pTunnelLength_},
                {"proxy", {{"host", i2pProxyHost_}, {"port", i2pProxyPort_}}},
            }},
    };
    // Written beside the file and renamed over it: a crash mid-write leaves the
    // previous settings rather than half a document.
    const std::filesystem::path scratch = path_.string() + ".new";
    {
        std::ofstream out(scratch, std::ios::trunc);
        out << document.dump(2) << '\n';
    }
    std::error_code failed;
    std::filesystem::rename(scratch, path_, failed);
    if (failed) {
        bazarish::log::warn("settings not written: {}", failed.message());
    }
}

std::string AppSettings::activeAccount() const { return activeAccount_; }

void AppSettings::setActiveAccount(const std::string& id)
{
    if (activeAccount_ == id) {
        return;
    }
    activeAccount_ = id;
    save();
}

std::vector<std::string> AppSettings::offlineAccounts() const { return offlineAccounts_; }

void AppSettings::setOfflineAccounts(std::vector<std::string> ids)
{
    if (offlineAccounts_ == ids) {
        return;
    }
    offlineAccounts_ = std::move(ids);
    save();
}

bool AppSettings::fullPrivacy() const { return fullPrivacy_; }

void AppSettings::setFullPrivacy(const bool on)
{
    if (fullPrivacy_ == on) {
        return;
    }
    fullPrivacy_ = on;
    save();
}

bool AppSettings::notifications() const { return notifications_; }

void AppSettings::setNotifications(const bool on)
{
    if (notifications_ == on) {
        return;
    }
    notifications_ = on;
    save();
}

bool AppSettings::i2pLogging() const { return i2pLogging_; }

void AppSettings::setI2pLogging(const bool on)
{
    if (i2pLogging_ == on) {
        return;
    }
    i2pLogging_ = on;
    save();
}

int AppSettings::i2pTunnelLength() const { return i2pTunnelLength_; }

void AppSettings::setI2pTunnelLength(const int level)
{
    const int wanted = std::clamp(level, kMinTunnelLength, kMaxTunnelLength);
    if (i2pTunnelLength_ == wanted) {
        return;
    }
    i2pTunnelLength_ = wanted;
    save();
}

std::string AppSettings::i2pProxyHost() const { return i2pProxyHost_; }

int AppSettings::i2pProxyPort() const { return i2pProxyPort_; }

void AppSettings::setI2pProxy(const std::string& host, const int port)
{
    const bool clearing = host.empty() || port <= 0;
    const std::string wantedHost = clearing ? std::string() : host;
    const int wantedPort = clearing ? 0 : port;
    if (i2pProxyHost_ == wantedHost && i2pProxyPort_ == wantedPort) {
        return;
    }
    i2pProxyHost_ = wantedHost;
    i2pProxyPort_ = wantedPort;
    save();
}

}  // namespace bazarish::app
