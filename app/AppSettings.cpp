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
#include <ios>
#include <string>
#include <utility>

namespace bazarish::app {

namespace {

// The lowest tunnel-length level, and the count of them. Kept here rather than
// taken from the I2P layer: this file stores a number, and what it selects is
// the caller's business.
// Where a SAM router listens unless the user says otherwise. Kept here rather
// than taken from the SAM layer: this file stores a number, and the meaning of
// it is the caller's business.
constexpr const char* kDefaultSamHost = "127.0.0.1";
constexpr int kDefaultSamPort = 7656;

constexpr int kMinTunnelLength = 0;
constexpr int kMaxTunnelLength = 2;

// A settings document is a few hundred bytes; the only part that grows is the
// list of switched-off accounts. Whatever sits at the path past this size is
// not settings, and parsing it would spend memory on someone else's file.
constexpr std::streamsize kMaxSettingsBytes = 64 * 1024;

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
    std::ifstream in(path_, std::ios::binary);
    if (!in.good()) {
        return;  // no file yet: the defaults above are the settings
    }
    // One byte past the limit is read so an oversized file is recognised as
    // such, and nothing larger is ever held in memory.
    std::string text(static_cast<std::size_t>(kMaxSettingsBytes) + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize taken = in.gcount();
    if (taken > kMaxSettingsBytes) {
        bazarish::log::warn("settings not read, using defaults: file is over {} bytes",
            kMaxSettingsBytes);
        return;
    }
    text.resize(static_cast<std::size_t>(taken));
    try {
        const nlohmann::json document = nlohmann::json::parse(text);
        activeAccount_ = document.value("activeAccount", std::string());
        offlineAccounts_ = document.value("offlineAccounts", std::vector<std::string>());
        notifications_ = document.value("notifications", true);
        backgroundTasks_ = document.value("backgroundTasks", false);
        const nlohmann::json i2p = document.value("i2p", nlohmann::json::object());
        i2pLogging_ = i2p.value("logging", false);
        i2pTunnelLength_
            = std::clamp(i2p.value("tunnelLength", kMinTunnelLength), kMinTunnelLength,
                kMaxTunnelLength);
        if (i2p.contains("sam")) {
            const nlohmann::json sam = i2p.at("sam");
            samEnabled_ = sam.value("enabled", false);
            samHost_ = sam.value("host", std::string(kDefaultSamHost));
            samPort_ = sam.value("port", kDefaultSamPort);
        }
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
    nlohmann::json i2p = {
        {"logging", i2pLogging_},
        {"tunnelLength", i2pTunnelLength_},
        {"proxy", {{"host", i2pProxyHost_}, {"port", i2pProxyPort_}}},
    };
    i2p["sam"] = {{"enabled", samEnabled_}, {"host", samHost_}, {"port", samPort_}};
    const nlohmann::json document = {
        {"activeAccount", activeAccount_},
        {"offlineAccounts", offlineAccounts_},
        {"notifications", notifications_},
        {"backgroundTasks", backgroundTasks_},
        {"i2p", i2p},
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

bool AppSettings::notifications() const { return notifications_; }

void AppSettings::setNotifications(const bool on)
{
    if (notifications_ == on) {
        return;
    }
    notifications_ = on;
    save();
}

bool AppSettings::backgroundTasks() const { return backgroundTasks_; }

void AppSettings::setBackgroundTasks(const bool on)
{
    if (backgroundTasks_ == on) {
        return;
    }
    backgroundTasks_ = on;
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

bool AppSettings::samEnabled() const { return samEnabled_; }

std::string AppSettings::samHost() const
{
    return samHost_.empty() ? std::string(kDefaultSamHost) : samHost_;
}

int AppSettings::samPort() const { return samPort_ > 0 ? samPort_ : kDefaultSamPort; }

void AppSettings::setSam(const bool enabled, const std::string& host, const int port)
{
    const std::string wantedHost = host.empty() ? std::string(kDefaultSamHost) : host;
    const int wantedPort = port > 0 ? port : kDefaultSamPort;
    if (samEnabled_ == enabled && samHost_ == wantedHost && samPort_ == wantedPort) {
        return;
    }
    samEnabled_ = enabled;
    samHost_ = wantedHost;
    samPort_ = wantedPort;
    save();
}

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
