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

    // A repeat of a name the manager can read is rejected.
    CHECK_THROWS(manager.create("Work Alias"));
    // A profile with a passphrase does not tell the manager its name, so a repeat
    // of it cannot be recognised as one: it gets its own directory instead.
    const ProfileInfo twin = manager.create("Acetone", "other");
    CHECK(twin.id == "acetone-2");
    CHECK(twin.name == "Acetone");

    // A name with no ASCII in it has nothing to derive an id from, so it falls
    // back to a generic one - and the next such name takes the next free id
    // instead of colliding with it.
    const ProfileInfo cyrillic = manager.create("клирнет");
    CHECK(cyrillic.id == "profile");
    CHECK(cyrillic.name == "клирнет");
    const ProfileInfo another = manager.create("тестовый");
    CHECK(another.id == "profile-2");
    CHECK(another.name == "тестовый");

    // Listing gives up nothing about a profile that has a passphrase: everything
    // it could say lives inside the keyed database. It is listed by its directory
    // id, marked locked, with no fingerprint. A profile without a passphrase opens
    // with the default key, so its name and fingerprint do show.
    CHECK(manager.list().size() == 5);
    for (const ProfileInfo& listed : manager.list()) {
        if (listed.id == "acetone") {
            CHECK(listed.encrypted);
            CHECK(listed.name == "acetone");
            CHECK(listed.fingerprint.empty());
        }
        if (listed.id == "work-alias") {
            CHECK(!listed.encrypted);
            CHECK(listed.name == "Work Alias");
            CHECK(!listed.fingerprint.empty());
        }
    }

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

    // Whether a locked profile has a server is part of what its database keeps,
    // so the listing cannot say: it only reports the profile as locked. With the
    // passphrase in hand the full picture is there.
    bool foundLocked = false;
    for (const ProfileInfo& info : manager.list()) {
        if (info.id == "acetone") {
            CHECK(info.encrypted);
            CHECK(!info.connected);
            foundLocked = true;
        }
    }
    CHECK(foundLocked);

    // Reopening preserves the connection and label.
    const Session reopened = manager.open("acetone", "secret");
    CHECK(reopened.isConnected());
    CHECK(reopened.endpoint().serverFingerprint == "serverfp");

    // Export the encrypted profile, then re-import it twice - once with an
    // at-rest passphrase, once without - and check that each import is one keyed
    // database that opens with its own key and nothing else.
    const auto onlyTheDatabase = [](const fs::path& dir) {
        int files = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
            CHECK(entry.path().filename() == "profile.db");
            ++files;
        }
        CHECK(files == 1);
    };

    // Kept outside the manager root so the imported profiles do not show up in
    // manager.list().
    const fs::path scratch
        = fs::temp_directory_path() / ("bazarish-export-" + toHex(randomBytes(8)));
    fs::create_directories(scratch);
    const fs::path bundle = scratch / "acetone.bundle";
    sa.exportProfile(bundle, "bundle-pw");

    Session::importProfile(bundle, scratch / "imported-enc", "bundle-pw", "atrest-pw");
    onlyTheDatabase(scratch / "imported-enc");
    const Session importedEnc = Session::open(scratch / "imported-enc", "atrest-pw");
    CHECK(importedEnc.fingerprint() == a.fingerprint);
    CHECK_THROWS(Session::open(scratch / "imported-enc"));

    Session::importProfile(bundle, scratch / "imported-plain", "bundle-pw");
    onlyTheDatabase(scratch / "imported-plain");
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
    CHECK(manager.list().size() == 4);

    fs::remove_all(root);
    std::fprintf(stderr, "TestProfile passed\n");
    return 0;
}
