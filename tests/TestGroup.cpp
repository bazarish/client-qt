// Bazarish project (c) 2026
#include "Session.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

using namespace bazarish;
using namespace bazarish::client;

namespace {

namespace fs = std::filesystem;

// Builds a roster body the way Session does and signs it with `signer`.
nlohmann::json rosterBody(const std::string& groupId, const Identity& admin, const Identity& member)
{
    return {
        {"v", 1},
        {"groupId", groupId},
        {"name", "Test Group"},
        {"epoch", 1},
        {"members",
            nlohmann::json::array(
                {{{"fp", admin.fingerprint()}, {"admin", true}},
                    {{"fp", member.fingerprint()}, {"admin", false}}})},
        {"admins", nlohmann::json::array({admin.fingerprint()})},
    };
}

// Mirrors Session::applyRoster's trust decision: the embedded signer must be in
// the roster's admin set.
bool rosterAccepted(const Bytes& signedDer, const std::string& groupId)
{
    const cms::VerifiedHybridJson verified = cms::verifyJsonHybrid(signedDer);
    if (verified.body.at("groupId").get<std::string>() != groupId) {
        return false;
    }
    for (const nlohmann::json& admin : verified.body.at("admins")) {
        if (admin.get<std::string>() == verified.identityFingerprint) {
            return true;
        }
    }
    return false;
}

// A roster signed by a current admin is accepted; one signed by a non-admin
// member is rejected. This is the governance invariant the group protocol relies
// on (only admins author the roster).
void testRosterTrust()
{
    const Identity admin = Identity::generate();
    const Identity member = Identity::generate();
    const std::string groupId = "abc123";

    const Bytes byAdmin = cms::signJsonHybrid(rosterBody(groupId, admin, member), admin);
    CHECK(rosterAccepted(byAdmin, groupId));
    // The verified signer is exactly the admin.
    CHECK(cms::verifyJsonHybrid(byAdmin).identityFingerprint == admin.fingerprint());

    // The same body, but signed by the non-admin member: must be refused.
    const Bytes byMember = cms::signJsonHybrid(rosterBody(groupId, admin, member), member);
    CHECK(!rosterAccepted(byMember, groupId));

    // A wrong-group roster is refused even when validly admin-signed.
    CHECK(!rosterAccepted(byAdmin, "different-group"));
}

// A tampered signed roster fails verification outright.
void testTamperedRosterRejected()
{
    const Identity admin = Identity::generate();
    const Identity member = Identity::generate();
    Bytes signed_ = cms::signJsonHybrid(rosterBody("g1", admin, member), admin);
    signed_[signed_.size() / 2] ^= 0x01;
    bool threw = false;
    try {
        cms::verifyJsonHybrid(signed_);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

// A connection-less session has no groups, and the group queries are safe on
// unknown ids.
void testEmptyGroupApi()
{
    const fs::path stateDir = fs::temp_directory_path() / "bz-testgroup-state";
    fs::remove_all(stateDir);
    const Session session = Session::create(stateDir, std::string{}, "tester");
    CHECK(session.groupIds().empty());
    CHECK(session.groupName("nope").empty());
    CHECK(session.groupMemberFingerprints("nope").empty());
    fs::remove_all(stateDir);
}

}  // namespace

int main()
{
    testRosterTrust();
    testTamperedRosterRejected();
    testEmptyGroupApi();
    std::fprintf(stderr, "TestGroup passed\n");
    return 0;
}
