// Bazarish project (c) 2026
#include "AppSettings.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

constexpr std::size_t kOversizedNoteBytes = 128 * 1024;

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

int main(int argc, char** argv)
{
    if (argc > 1) {
        const std::string mode = argv[1];
        const AppSettings& loaded = AppSettings::instance();
        if (mode == "expect-active") {
            CHECK(loaded.activeAccount() == "alice");
            CHECK(loaded.backgroundTasks());
        } else {
            CHECK(mode == "expect-default");
            CHECK(loaded.activeAccount().empty());
            CHECK(!loaded.backgroundTasks());
        }
        return 0;
    }

    const fs::path root = fs::temp_directory_path() / "bazarish-settings-test";
    fs::remove_all(root);
    fs::create_directories(root / "accounts");
    const std::string accountsDir = (root / "accounts").string();
#ifdef _WIN32
    CHECK(::_putenv_s("BAZARISH_ACCOUNTS_DIR", accountsDir.c_str()) == 0);
#else
    CHECK(::setenv("BAZARISH_ACCOUNTS_DIR", accountsDir.c_str(), 1) == 0);
#endif
    const fs::path file = root / "settings.json";

    AppSettings& settings = AppSettings::instance();

    CHECK(settings.activeAccount().empty());
    CHECK(settings.offlineAccounts().empty());
    CHECK(settings.notifications());
    CHECK(!settings.backgroundTasks());
    CHECK(!settings.i2pLogging());
    CHECK(settings.i2pTunnelLength() == 0);
    CHECK(settings.i2pProxyHost().empty());
    CHECK(settings.i2pProxyPort() == 0);
    CHECK(!settings.samEnabled());
    CHECK(settings.samHost() == "127.0.0.1");
    CHECK(settings.samPort() == 7656);
    CHECK(!fs::exists(file));

    settings.setActiveAccount("alice");
    settings.setOfflineAccounts({"bob", "carol"});
    settings.setNotifications(false);
    settings.setBackgroundTasks(true);
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
        CHECK(document.at("notifications") == false);
        CHECK(document.at("backgroundTasks") == true);
        CHECK(document.at("i2p").at("logging") == true);
        CHECK(document.at("i2p").at("tunnelLength") == 2);
        CHECK(document.at("i2p").at("proxy").at("host") == "127.0.0.1");
        CHECK(document.at("i2p").at("proxy").at("port") == 9050);
    }

    settings.setI2pTunnelLength(7);
    CHECK(settings.i2pTunnelLength() == 2);

    settings.setI2pProxy("127.0.0.1", 0);
    CHECK(settings.i2pProxyHost().empty());
    CHECK(settings.i2pProxyPort() == 0);

    const auto readsAs = [self = fs::absolute(argv[0]).string()](const char* const mode) {
        const std::string command = "\"" + self + "\" " + mode;
        return std::system(command.c_str()) == 0;
    };

    const nlohmann::json document = {{"activeAccount", "alice"}, {"backgroundTasks", true}};
    {
        std::ofstream out(file, std::ios::trunc);
        out << document.dump();
    }
    CHECK(readsAs("expect-active"));

    {
        nlohmann::json oversized = document;
        oversized["note"] = std::string(kOversizedNoteBytes, 'x');
        std::ofstream out(file, std::ios::trunc);
        out << oversized.dump();
    }
    CHECK(fs::file_size(file) > kOversizedNoteBytes);
    CHECK(readsAs("expect-default"));

    {
        std::ofstream out(file, std::ios::trunc);
        out << "{ this is not json";
    }
    CHECK(readsAs("expect-default"));

    fs::remove_all(root);
    std::fprintf(stderr, "TestAppSettings passed\n");
    return 0;
}
