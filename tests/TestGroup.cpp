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

// Per-message sender signing: a group message carries a hybrid signature
// (`gsig`) binding its `from` + content to a signing identity, so a member cannot
// forge another member's `from`. Exercises the real Session::authenticateGroupSender.
void testGroupMessageSenderAuth()
{
    const Identity alice = Identity::generate();    // an honest member (the sender)
    const Identity mallory = Identity::generate();  // a member who tries to forge
    const Identity victim = Identity::generate();   // the member mallory impersonates
    const std::string groupId = "g-sign";
    const std::int64_t sentAt = 1000;
    const std::string id = "msg1";
    const std::string text = "hello group";

    std::map<std::string, GroupMember> members;
    members[alice.fingerprint()] = {};
    members[mallory.fingerprint()] = {};
    members[victim.fingerprint()] = {};

    const auto signedBody = [&](const std::string& from, const std::string& t) {
        return nlohmann::json{{"type", "text"}, {"id", id}, {"from", from}, {"groupId", groupId},
            {"sentAt", sentAt}, {"text", t}};
    };
    const auto inner = [&](const std::string& from, const std::string& t, const Bytes& gsig) {
        return nlohmann::json{{"v", 1}, {"type", "text"}, {"id", id}, {"from", from},
            {"sentAt", sentAt}, {"text", t}, {"group", {{"id", groupId}}}, {"gsig", toBase64(gsig)}};
    };

    // 1. Honest: alice signs her own message - authenticated as alice.
    {
        const Bytes gsig = cms::signJsonHybrid(signedBody(alice.fingerprint(), text), alice);
        const auto from
            = Session::authenticateGroupSender(inner(alice.fingerprint(), text, gsig), "text", id,
                groupId, &members);
        CHECK(from.has_value() && *from == alice.fingerprint());
    }

    // 2a. Forge: mallory signs a body that lies `from`=victim - signer != signed-from -> rejected.
    {
        const Bytes gsig = cms::signJsonHybrid(signedBody(victim.fingerprint(), text), mallory);
        CHECK(!Session::authenticateGroupSender(inner(victim.fingerprint(), text, gsig), "text", id,
            groupId, &members)
                   .has_value());
    }

    // 2b. Forge: mallory signs honestly (from=mallory) but sets the OUTER from=victim - the
    //     outer claim must match the signed one -> rejected.
    {
        const Bytes gsig = cms::signJsonHybrid(signedBody(mallory.fingerprint(), text), mallory);
        CHECK(!Session::authenticateGroupSender(inner(victim.fingerprint(), text, gsig), "text", id,
            groupId, &members)
                   .has_value());
    }

    // 3. Tamper: a valid signature over the original text, but the outer text was changed -> rejected.
    {
        const Bytes gsig = cms::signJsonHybrid(signedBody(alice.fingerprint(), text), alice);
        CHECK(!Session::authenticateGroupSender(inner(alice.fingerprint(), "tampered", gsig), "text",
            id, groupId, &members)
                   .has_value());
    }

    // 4. Non-member: a correctly self-signed message from someone not in the group -> rejected.
    {
        std::map<std::string, GroupMember> withoutAlice;
        withoutAlice[mallory.fingerprint()] = {};
        const Bytes gsig = cms::signJsonHybrid(signedBody(alice.fingerprint(), text), alice);
        CHECK(!Session::authenticateGroupSender(inner(alice.fingerprint(), text, gsig), "text", id,
            groupId, &withoutAlice)
                   .has_value());
    }

    // 5. Unsigned: no `gsig` at all -> rejected.
    {
        const nlohmann::json m = {{"v", 1}, {"type", "text"}, {"id", id},
            {"from", alice.fingerprint()}, {"sentAt", sentAt}, {"text", text},
            {"group", {{"id", groupId}}}};
        CHECK(!Session::authenticateGroupSender(m, "text", id, groupId, &members).has_value());
    }

    // 6. Unknown group (members == nullptr): membership is deferred (roster not applied yet), but
    //    a valid signature is still required and from-authenticity enforced.
    {
        const Bytes gsig = cms::signJsonHybrid(signedBody(alice.fingerprint(), text), alice);
        const auto from = Session::authenticateGroupSender(inner(alice.fingerprint(), text, gsig),
            "text", id, groupId, nullptr);
        CHECK(from.has_value() && *from == alice.fingerprint());
    }
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
    testGroupMessageSenderAuth();
    testEmptyGroupApi();
    std::fprintf(stderr, "TestGroup passed\n");
    return 0;
}
