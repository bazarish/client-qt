// Bazarish project (c) 2026
#include "ProfileManager.hpp"
#include "Session.hpp"

#include <bazarish/Bytes.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

#define CHECK_THROWS(expression)                                                     \
    do {                                                                             \
        bool thrown = false;                                                         \
        try {                                                                        \
            (void)(expression);                                                      \
        } catch (const std::exception&) {                                            \
            thrown = true;                                                           \
        }                                                                            \
        if (!thrown) {                                                               \
            std::fprintf(stderr, "CHECK_THROWS failed at %s:%d\n", __FILE__, __LINE__); \
            std::exit(1);                                                            \
        }                                                                            \
    } while (false)

using namespace bazarish;
using namespace bazarish::client;

int main()
{
    namespace fs = std::filesystem;
    const fs::path root
        = fs::temp_directory_path() / ("bazarish-profiles-" + toHex(randomBytes(8)));

    ProfileManager manager(root);
    CHECK(manager.list().empty());

    // Create two profiles, one encrypted; the id is derived from the name.
    const ProfileInfo a = manager.create("Acetone", "secret");
    CHECK(a.id == "acetone");
    CHECK(a.name == "Acetone");
    CHECK(a.encrypted);
    CHECK(!a.connected);
    CHECK(a.fingerprint.size() == kFingerprintTextLength);

    const ProfileInfo b = manager.create("Work Alias");
    CHECK(b.id == "work-alias");
    CHECK(!b.encrypted);

    // Duplicate name is rejected.
    CHECK_THROWS(manager.create("Acetone", "x"));

    // Listing reads public metadata with no passphrase.
    CHECK(manager.list().size() == 2);

    // Encrypted profile needs its passphrase to open.
    CHECK_THROWS(manager.open("acetone"));
    Session sa = manager.open("acetone", "secret");
    CHECK(sa.fingerprint() == a.fingerprint);
    CHECK(sa.displayName() == "Acetone");
    CHECK(!sa.isConnected());

    // Connecting a profile to a server persists the endpoint and flips the
    // connected flag seen by the picker.
    ServerEndpoint endpoint;
    endpoint.host = "127.0.0.1";
    endpoint.port = 18000;
    endpoint.serverFingerprint = "serverfp";
    sa.connectServer(endpoint);
    CHECK(sa.isConnected());
    CHECK(sa.endpoint().port == 18000);

    bool foundConnected = false;
    for (const ProfileInfo& info : manager.list()) {
        if (info.id == "acetone") {
            CHECK(info.connected);
            foundConnected = true;
        }
    }
    CHECK(foundConnected);

    // Reopening preserves the connection and label.
    const Session reopened = manager.open("acetone", "secret");
    CHECK(reopened.isConnected());
    CHECK(reopened.endpoint().serverFingerprint == "serverfp");

    // Removal drops the profile.
    manager.remove("work-alias");
    CHECK(!manager.exists("work-alias"));
    CHECK(manager.list().size() == 1);

    fs::remove_all(root);
    std::fprintf(stderr, "TestProfile passed\n");
    return 0;
}
