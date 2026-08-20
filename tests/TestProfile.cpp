// Bazarish project (c) 2026
#include "ProfileManager.hpp"
#include "Session.hpp"

#include <bazarish/Bytes.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    // A name with no ASCII in it has nothing to derive an id from, so it falls
    // back to a generic one - and the next such name takes the next free id
    // instead of colliding with it.
    const ProfileInfo cyrillic = manager.create("клирнет");
    CHECK(cyrillic.id == "profile");
    CHECK(cyrillic.name == "клирнет");
    const ProfileInfo another = manager.create("тестовый");
    CHECK(another.id == "profile-2");
    CHECK(another.name == "тестовый");

    // Listing reads public metadata with no passphrase.
    CHECK(manager.list().size() == 4);

    // Encrypted profile needs its passphrase to open.
    CHECK_THROWS(manager.open("acetone"));
    Session sa = manager.open("acetone", "secret");
    CHECK(sa.fingerprint() == a.fingerprint);
    CHECK(sa.displayName() == "Acetone");
    CHECK(!sa.isConnected());

    // Connecting a profile to a server persists the endpoint and flips the
    // connected flag seen by the picker.
    ServerEndpoint endpoint;
    endpoint.serverFingerprint = "serverfp";
    endpoint.facades = {Facade{false, "127.0.0.1", 18000, {}}};
    sa.connectServer(endpoint);
    CHECK(sa.isConnected());
    CHECK(sa.endpoint().facades.at(0).port == 18000);

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

    // Export the encrypted profile, then re-import it twice - once with an
    // at-rest passphrase, once without - to check the contacts file matches the
    // chosen scheme (sealed CMS DER vs plaintext JSON) and still reopens.
    const auto firstByte = [](const fs::path& path) -> unsigned char {
        std::ifstream in(path, std::ios::binary);
        CHECK(in.good());
        char c = 0;
        in.read(&c, 1);
        return static_cast<unsigned char>(c);
    };

    // Kept outside the manager root so the imported profiles do not show up in
    // manager.list().
    const fs::path scratch
        = fs::temp_directory_path() / ("bazarish-export-" + toHex(randomBytes(8)));
    fs::create_directories(scratch);
    const fs::path bundle = scratch / "acetone.bundle";
    sa.exportProfile(bundle, "bundle-pw");

    Session::importProfile(bundle, scratch / "imported-enc", "bundle-pw", "atrest-pw");
    // CMS DER begins with the SEQUENCE tag 0x30, never the '{' of plaintext JSON.
    CHECK(firstByte(scratch / "imported-enc" / "contacts.json") == 0x30);
    const Session importedEnc = Session::open(scratch / "imported-enc", "atrest-pw");
    CHECK(importedEnc.fingerprint() == a.fingerprint);
    CHECK_THROWS(Session::open(scratch / "imported-enc"));

    Session::importProfile(bundle, scratch / "imported-plain", "bundle-pw");
    CHECK(firstByte(scratch / "imported-plain" / "contacts.json") == '{');
    const Session importedPlain = Session::open(scratch / "imported-plain");
    CHECK(importedPlain.fingerprint() == a.fingerprint);

    // Importing through a manager restores the display name from the bundle. With
    // no explicit name the on-disk id is derived from that restored name; an
    // explicit name only chooses the id (the display name still comes from the
    // bundle). Use a fresh root so the derived "acetone" id does not collide.
    const fs::path root2
        = fs::temp_directory_path() / ("bazarish-profiles-" + toHex(randomBytes(8)));
    ProfileManager manager2(root2);
    const ProfileInfo imp1 = manager2.import("", bundle, "bundle-pw");
    CHECK(imp1.id == "acetone");
    CHECK(imp1.name == "Acetone");
    CHECK(imp1.fingerprint == a.fingerprint);
    const ProfileInfo imp2 = manager2.import("Other Name", bundle, "bundle-pw");
    CHECK(imp2.id == "other-name");
    CHECK(imp2.name == "Acetone");
    CHECK(manager2.list().size() == 2);
    CHECK(!fs::exists(root2 / ".import-tmp"));
    fs::remove_all(root2);

    fs::remove_all(scratch);

    // Removal drops the profile.
    manager.remove("work-alias");
    CHECK(!manager.exists("work-alias"));
    CHECK(manager.list().size() == 3);

    fs::remove_all(root);
    std::fprintf(stderr, "TestProfile passed\n");
    return 0;
}
