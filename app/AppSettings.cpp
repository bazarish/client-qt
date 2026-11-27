// Bazarish project (c) 2026
#include "AppSettings.hpp"

#include "AccountManager.hpp"

#include <bazarish/Log.hpp>
#include <bazarish/PrivateFile.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <utility>

namespace bazarish::app {

namespace {

constexpr const char* kDefaultSamHost = "127.0.0.1";
constexpr int kDefaultSamPort = 7656;

constexpr int kMinTunnelLength = 0;
constexpr int kMaxTunnelLength = 2;

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

std::filesystem::path i2pRoot()
{
    return appRoot() / "i2p";
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
        return;
    }
    std::string text(static_cast<std::size_t>(kMaxSettingsBytes) + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize taken = in.gcount();
    if (taken > kMaxSettingsBytes) {
        unreadable_ = "the file is over " + std::to_string(kMaxSettingsBytes) + " bytes";
        return;
    }
    text.resize(static_cast<std::size_t>(taken));
    try {
        const nlohmann::json document = nlohmann::json::parse(text);
        activeAccount_ = document.value("activeAccount", std::string());
        language_ = document.value("language", std::string());
        lightTheme_ = document.value("lightTheme", true);
        camera_ = document.value("camera", std::string());
        screenCapture_ = document.value("screenCapture", false);
        offlineAccounts_ = document.value("offlineAccounts", std::vector<std::string>());
        notifications_ = document.value("notifications", true);
        notificationText_ = document.value("notificationText", true);
        systemNotifications_ = document.value("systemNotifications", false);
        backgroundTasks_ = document.value("backgroundTasks", false);
        const nlohmann::json audio = document.value("audio", nlohmann::json::object());
        audioInput_ = audio.value("input", std::string());
        audioOutput_ = audio.value("output", std::string());
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
        if (i2p.contains("gateway")) {
            const nlohmann::json gateway = i2p.at("gateway");
            gatewayAsked_ = gateway.value("asked", false);
            gatewayEnabled_ = gateway.value("enabled", false);
            gatewayAddress_ = gateway.value("address", std::string());
        }
        const nlohmann::json proxy = i2p.value("proxy", nlohmann::json::object());
        i2pProxyHost_ = proxy.value("host", std::string());
        i2pProxyPort_ = proxy.value("port", 0);
        if (i2pProxyHost_.empty() || i2pProxyPort_ <= 0) {
            i2pProxyHost_.clear();
            i2pProxyPort_ = 0;
        }
        const nlohmann::json lock = document.value("lock", nlohmann::json::object());
        if (lock.contains("pin")) {
            const nlohmann::json& pin = lock.at("pin");
            UiLockPin read{fromBase64(pin.at("salt").get<std::string>()),
                fromBase64(pin.at("hash").get<std::string>())};
            if (!isUiLockPinRecord(read)) {
                unreadable_ = "the app lock PIN is malformed";
                return;
            }
            uiLockPin_ = std::move(read);
        }
        uiAutoLockMinutes_ = lock.value("autoLockMinutes", 0);
        if (!isUiAutoLockChoice(uiAutoLockMinutes_)) {
            unreadable_ = "the app lock names an automatic lock of "
                + std::to_string(uiAutoLockMinutes_) + " minutes";
            return;
        }
        uiLockFingerprint_ = lock.value("fingerprint", true);
        uiLockHidesAll_ = lock.value("hideAllText", false);
        uiLockPhoto_ = lock.value("photo", true);
    } catch (const std::exception& error) {
        unreadable_ = error.what();
    }
}

std::string AppSettings::unreadable() const
{
    return unreadable_;
}

const std::filesystem::path& AppSettings::path() const
{
    return path_;
}

void AppSettings::save() const
{
    nlohmann::json i2p = {
        {"logging", i2pLogging_},
        {"tunnelLength", i2pTunnelLength_},
        {"proxy", {{"host", i2pProxyHost_}, {"port", i2pProxyPort_}}},
    };
    i2p["sam"] = {{"enabled", samEnabled_}, {"host", samHost_}, {"port", samPort_}};
    i2p["gateway"] = {{"asked", gatewayAsked_}, {"enabled", gatewayEnabled_},
        {"address", gatewayAddress_}};
    nlohmann::json lock = {
        {"autoLockMinutes", uiAutoLockMinutes_},
        {"fingerprint", uiLockFingerprint_},
        {"hideAllText", uiLockHidesAll_},
        {"photo", uiLockPhoto_},
    };
    if (uiLockPin_.has_value()) {
        lock["pin"] = {{"salt", toBase64(uiLockPin_->salt)}, {"hash", toBase64(uiLockPin_->hash)}};
    }
    const nlohmann::json document = {
        {"activeAccount", activeAccount_},
        {"language", language_},
        {"lightTheme", lightTheme_},
        {"camera", camera_},
        {"screenCapture", screenCapture_},
        {"offlineAccounts", offlineAccounts_},
        {"notifications", notifications_},
        {"notificationText", notificationText_},
        {"systemNotifications", systemNotifications_},
        {"backgroundTasks", backgroundTasks_},
        {"audio", {{"input", audioInput_}, {"output", audioOutput_}}},
        {"i2p", i2p},
        {"lock", lock},
    };
    try {
        bazarish::writePrivateFile(path_, document.dump(2) + "\n");
    } catch (const std::exception& error) {
        bazarish::log::warn("settings not written: {}", error.what());
    }
}

std::string AppSettings::activeAccount() const { return activeAccount_; }

std::string AppSettings::language() const { return language_; }

void AppSettings::setLanguage(const std::string& code)
{
    if (language_ == code) {
        return;
    }
    language_ = code;
    save();
}

bool AppSettings::lightTheme() const { return lightTheme_; }

void AppSettings::setLightTheme(const bool on)
{
    if (lightTheme_ == on) {
        return;
    }
    lightTheme_ = on;
    save();
}

std::string AppSettings::camera() const { return camera_; }

void AppSettings::setCamera(const std::string& id)
{
    if (camera_ == id) {
        return;
    }
    camera_ = id;
    save();
}

bool AppSettings::screenCapture() const { return screenCapture_; }

void AppSettings::setScreenCapture(const bool on)
{
    if (screenCapture_ == on) {
        return;
    }
    screenCapture_ = on;
    save();
}

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

bool AppSettings::notificationText() const { return notificationText_; }

void AppSettings::setNotificationText(const bool on)
{
    if (notificationText_ == on) {
        return;
    }
    notificationText_ = on;
    save();
}

bool AppSettings::systemNotifications() const { return systemNotifications_; }

void AppSettings::setSystemNotifications(const bool on)
{
    if (systemNotifications_ == on) {
        return;
    }
    systemNotifications_ = on;
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

std::string AppSettings::audioInput() const { return audioInput_; }

void AppSettings::setAudioInput(const std::string& id)
{
    if (audioInput_ == id) {
        return;
    }
    audioInput_ = id;
    save();
}

std::string AppSettings::audioOutput() const { return audioOutput_; }

void AppSettings::setAudioOutput(const std::string& id)
{
    if (audioOutput_ == id) {
        return;
    }
    audioOutput_ = id;
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

bool AppSettings::gatewayAsked() const { return gatewayAsked_; }

bool AppSettings::gatewayEnabled() const { return gatewayEnabled_; }

std::string AppSettings::gatewayAddress() const { return gatewayAddress_; }

void AppSettings::rememberGateway(const std::string& address)
{
    gatewayAsked_ = true;
    gatewayAddress_ = address;
    save();
}

void AppSettings::useGateway(const bool on)
{
    gatewayAsked_ = true;
    gatewayEnabled_ = on && !gatewayAddress_.empty();
    if (gatewayEnabled_) {
        samEnabled_ = false;
    }
    save();
}

void AppSettings::skipGateway()
{
    gatewayAsked_ = true;
    gatewayEnabled_ = false;
    save();
}

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
    if (samEnabled_) {
        gatewayEnabled_ = false;
    }
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

void AppSettings::setUiLockPin(std::optional<UiLockPin> pin)
{
    uiLockPin_ = std::move(pin);
    save();
}

void AppSettings::setUiAutoLockMinutes(const int minutes)
{
    if (!isUiAutoLockChoice(minutes)) {
        throw std::invalid_argument("no automatic lock after " + std::to_string(minutes) + " minutes");
    }
    if (uiAutoLockMinutes_ == minutes) {
        return;
    }
    uiAutoLockMinutes_ = minutes;
    save();
}

void AppSettings::setUiLockFingerprint(const bool on)
{
    if (uiLockFingerprint_ == on) {
        return;
    }
    uiLockFingerprint_ = on;
    save();
}

void AppSettings::setUiLockHidesAll(const bool on)
{
    if (uiLockHidesAll_ == on) {
        return;
    }
    uiLockHidesAll_ = on;
    save();
}

void AppSettings::setUiLockPhoto(const bool on)
{
    if (uiLockPhoto_ == on) {
        return;
    }
    uiLockPhoto_ = on;
    save();
}

}  // namespace bazarish::app
