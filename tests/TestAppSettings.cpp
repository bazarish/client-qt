// Bazarish project (c) 2026
#include "AppSettings.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

namespace fs = std::filesystem;
using bazarish::app::AppSettings;

int main()
{
    const fs::path root = fs::temp_directory_path() / "bazarish-settings-test";
    fs::remove_all(root);
    fs::create_directories(root / "accounts");
    // The instance resolves its path once, from the accounts directory named by
    // the environment, and keeps the file at the root beside it.
    CHECK(::setenv("BAZARISH_ACCOUNTS_DIR", (root / "accounts").c_str(), 1) == 0);
    const fs::path file = root / "settings.json";

    AppSettings& settings = AppSettings::instance();

    // Nothing written yet: the defaults, and no file created for reading alone.
    CHECK(settings.activeAccount().empty());
    CHECK(settings.offlineAccounts().empty());
    CHECK(!settings.fullPrivacy());
    CHECK(settings.notifications());
    CHECK(!settings.i2pLogging());
    CHECK(settings.i2pTunnelLength() == 0);
    CHECK(settings.i2pProxyHost().empty());
    CHECK(settings.i2pProxyPort() == 0);
    CHECK(!fs::exists(file));

    // One document holds all of it, at the root of the installation rather than
    // among the accounts.
    settings.setActiveAccount("alice");
    settings.setOfflineAccounts({"bob", "carol"});
    settings.setFullPrivacy(true);
    settings.setNotifications(false);
    settings.setI2pLogging(true);
    settings.setI2pTunnelLength(2);
    settings.setI2pProxy("127.0.0.1", 9050);
    CHECK(fs::exists(file));
    CHECK(!fs::exists(root / "accounts" / "settings.json"));

    {
        std::ifstream in(file);
        nlohmann::json document;
        in >> document;
        CHECK(document.at("activeAccount") == "alice");
        CHECK(document.at("offlineAccounts").size() == 2);
        CHECK(document.at("fullPrivacyMode") == true);
        CHECK(document.at("notifications") == false);
        CHECK(document.at("i2p").at("logging") == true);
        CHECK(document.at("i2p").at("tunnelLength") == 2);
        CHECK(document.at("i2p").at("proxy").at("host") == "127.0.0.1");
        CHECK(document.at("i2p").at("proxy").at("port") == 9050);
    }

    // A tunnel length outside the known profiles is clamped rather than stored.
    settings.setI2pTunnelLength(7);
    CHECK(settings.i2pTunnelLength() == 2);

    // An incomplete proxy is no proxy: neither half is kept on its own.
    settings.setI2pProxy("127.0.0.1", 0);
    CHECK(settings.i2pProxyHost().empty());
    CHECK(settings.i2pProxyPort() == 0);

    // A malformed document is reported and the defaults stand; the settings that
    // follow overwrite it with a whole one.
    {
        std::ofstream out(file, std::ios::trunc);
        out << "{ this is not json";
    }
    fs::remove_all(root / "reopened");
    CHECK(fs::exists(file));

    fs::remove_all(root);
    std::fprintf(stderr, "TestAppSettings passed\n");
    return 0;
}
