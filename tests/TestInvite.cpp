// Bazarish project (c) 2026
#include "Qr.hpp"
#include "Session.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Certificates.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Descriptor.hpp>

#include <cstdio>
#include <cstdlib>
#include <ctime>
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
            std::fprintf(stderr, "CHECK_THROWS failed at %s:%d: %s did not throw\n", \
                __FILE__, __LINE__, #expression);                                    \
            std::exit(1);                                                            \
        }                                                                            \
    } while (false)

using namespace bazarish;
using namespace bazarish::client;

namespace {

namespace fs = std::filesystem;

fs::path uniqueTempDir(const std::string& tag)
{
    return fs::temp_directory_path() / ("bazarish-test-" + tag + "-" + toHex(randomBytes(8)));
}

}  // namespace

int main()
{
    // --- Invite (descriptor): encode/parse and QR rendering ---

    const Identity serverIdentity = Identity::generate();
    const std::string serverFp = serverIdentity.fingerprint();
    const Key servingKey = Key::generateSealing();
    const std::string userDest = "dlkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq.b32.i2p";
    const Identity user = Identity::generate();

    // The invite is a small descriptor (fingerprint + serving destination +
    // serving sealing key); the contact card is fetched and verified separately
    // (TestClient). The descriptor codec itself is covered by common TestDescriptor.
    const Descriptor descriptor{user.fingerprint(), userDest, servingKey.publicDer()};
    const std::string uri = encodeDescriptor(descriptor);
    CHECK(uri.rfind("bazarish://invite?", 0) == 0);

    const Descriptor decoded = parseDescriptor(uri);
    CHECK(decoded.fingerprint == user.fingerprint());
    CHECK(decoded.srv == userDest);
    CHECK(decoded.srvKeyDer == servingKey.publicDer());

    // Malformed URIs are rejected.
    CHECK_THROWS(parseDescriptor("http://example/x"));
    CHECK_THROWS(parseDescriptor("bazarish://invite?v=1&fp=short"));

    // The small descriptor renders as QR (one or a few frames — far smaller than
    // the retired full-card invite, which needed a multi-frame sequence).
    const std::vector<std::string> codes = renderQrCodes(uri);
    CHECK(!codes.empty());
    for (const std::string& code : codes) {
        CHECK(!code.empty());
    }

    // --- Session: at-rest passphrase and export/import (all offline) ---

    ServerEndpoint endpoint;
    endpoint.serverFingerprint = serverFp;
    endpoint.facades = {Facade{false, "127.0.0.1", 9, {}}};

    const fs::path dirA = uniqueTempDir("a");
    const std::string passphrase = "at-rest secret";
    const std::string fingerprintA = Session::create(dirA, endpoint, passphrase).fingerprint();

    // Encrypted keys cannot be opened without the passphrase, and a wrong one
    // fails too.
    CHECK_THROWS(Session::open(dirA));
    CHECK_THROWS(Session::open(dirA, "wrong"));
    CHECK(Session::open(dirA, passphrase).fingerprint() == fingerprintA);

    // Export to a password-protected bundle, then import into a fresh dir with
    // no at-rest passphrase: the identity survives the round trip.
    const fs::path bundle = uniqueTempDir("bundle") / "session.baz";
    fs::create_directories(bundle.parent_path());
    const std::string exportPw = "export password";
    Session::open(dirA, passphrase).exportState(bundle, exportPw);

    const fs::path dirB = uniqueTempDir("b");
    CHECK_THROWS(Session::importState(bundle, dirB, "bad password"));
    Session::importState(bundle, dirB, exportPw);
    // No at-rest passphrase on the imported copy: it opens with none.
    CHECK(Session::open(dirB).fingerprint() == fingerprintA);

    // Import again, this time re-encrypting at rest under a new passphrase.
    const fs::path dirC = uniqueTempDir("c");
    Session::importState(bundle, dirC, exportPw, "new at-rest");
    CHECK_THROWS(Session::open(dirC));
    CHECK(Session::open(dirC, "new at-rest").fingerprint() == fingerprintA);

    fs::remove_all(dirA);
    fs::remove_all(dirB);
    fs::remove_all(dirC);
    fs::remove_all(bundle.parent_path());

    std::fprintf(stderr, "TestInvite passed\n");
    return 0;
}
