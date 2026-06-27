// Bazarish project (c) 2026
#include "Session.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
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

// A roster body that lists every fingerprint in `adminFps` as an admin member
// (used to forge a "promote myself" roster).
nlohmann::json rosterBodyClaiming(
    const std::string& groupId, const std::vector<std::string>& adminFps, std::int64_t epoch)
{
    nlohmann::json admins = nlohmann::json::array();
    nlohmann::json members = nlohmann::json::array();
    for (const std::string& fp : adminFps) {
        admins.push_back(fp);
        members.push_back({{"fp", fp}, {"admin", true}});
    }
    return {{"v", 1}, {"groupId", groupId}, {"name", "g"}, {"epoch", epoch},
        {"members", members}, {"admins", admins}};
}

// Admin authority over a group's roster is ANCHORED, not self-asserted: a roster
// update to an established group is applied only when signed by an admin we
// already recognize. This is the governance invariant - without it a member could
// sign a roster naming themselves admin and seize the group. Exercises the real
// Session::isRosterUpdateAuthorized decision.
void testRosterTrust()
{
    const Identity admin = Identity::generate();
    const Identity member = Identity::generate();
    const std::string groupId = "abc123";

    // The signature authenticates the signer (sanity).
    const Bytes byAdmin = cms::signJsonHybrid(rosterBody(groupId, admin, member), admin);
    CHECK(cms::verifyJsonHybrid(byAdmin).identityFingerprint == admin.fingerprint());

    const nlohmann::json adminRoster = rosterBody(groupId, admin, member);
    const std::set<std::string> knownAdmins{admin.fingerprint()};  // we recognize `admin`

    // Established group: a roster signed by a CURRENT admin is applied.
    CHECK(Session::isRosterUpdateAuthorized(
        admin.fingerprint(), adminRoster, knownAdmins, /*established=*/true, /*bootstrap=*/false));

    // ESCALATION REGRESSION: the non-admin member forges a roster that lists
    // *themselves* as admin and signs it. The new roster vouches for them, but an
    // established group MUST refuse it - authority is anchored to who we already
    // know, never to the roster's own claims.
    const nlohmann::json selfPromote = rosterBodyClaiming(groupId, {member.fingerprint()}, 99);
    CHECK(!Session::isRosterUpdateAuthorized(
        member.fingerprint(), selfPromote, knownAdmins, /*established=*/true, /*bootstrap=*/false));
    // Even routed as a (forged) invite, it is refused for a group we already hold.
    CHECK(!Session::isRosterUpdateAuthorized(
        member.fingerprint(), selfPromote, knownAdmins, /*established=*/true, /*bootstrap=*/true));

    // First roster (an invite, trust on first use): a self-consistent roster
    // signed by one of its declared admins is accepted only on the bootstrap path.
    CHECK(Session::isRosterUpdateAuthorized(
        admin.fingerprint(), adminRoster, {}, /*established=*/false, /*bootstrap=*/true));
    // A bootstrap roster whose signer is not even in its own admins is refused.
    CHECK(!Session::isRosterUpdateAuthorized(
        member.fingerprint(), adminRoster, {}, /*established=*/false, /*bootstrap=*/true));
    // A non-bootstrap update for a group we do not hold is refused outright.
    CHECK(!Session::isRosterUpdateAuthorized(
        admin.fingerprint(), adminRoster, {}, /*established=*/false, /*bootstrap=*/false));
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
    const fs::path profileDir = fs::temp_directory_path() / "bz-testgroup-state";
    fs::remove_all(profileDir);
    const Session session = Session::create(profileDir, std::string{}, "tester");
    CHECK(session.groupIds().empty());
    CHECK(session.groupName("nope").empty());
    CHECK(session.groupMemberFingerprints("nope").empty());
    fs::remove_all(profileDir);
}

// A member can remove THEMSELVES with a signed `group.leave`, but cannot forge
// another member's removal. Self-service leaving is authenticated by the leaver's
// own signature, independent of any admin. Exercises Session::authenticateGroupLeave.
void testGroupLeaveAuth()
{
    const Identity a = Identity::generate();
    const Identity b = Identity::generate();
    const std::string gid = "g-leave";

    const auto leaveMsg = [&](const std::string& from, const Identity& signer) {
        const nlohmann::json signedBody
            = {{"type", "group.leave"}, {"groupId", gid}, {"from", from}};
        return nlohmann::json{{"v", 1}, {"type", "group.leave"}, {"from", from},
            {"groupId", gid}, {"gsig", toBase64(cms::signJsonHybrid(signedBody, signer))}};
    };

    // Self-service: A signs their own leave -> authorized to remove exactly A.
    {
        const auto who = Session::authenticateGroupLeave(leaveMsg(a.fingerprint(), a), gid);
        CHECK(who.has_value() && *who == a.fingerprint());
    }
    // Forge: B signs a leave whose `from` claims A -> signer != from -> refused.
    {
        CHECK(!Session::authenticateGroupLeave(leaveMsg(a.fingerprint(), b), gid).has_value());
    }
    // Unsigned leave -> refused (a bare `from` must never remove anyone).
    {
        const nlohmann::json m
            = {{"v", 1}, {"type", "group.leave"}, {"from", a.fingerprint()}, {"groupId", gid}};
        CHECK(!Session::authenticateGroupLeave(m, gid).has_value());
    }
    // Wrong group id -> refused even with a valid self-signature.
    {
        CHECK(!Session::authenticateGroupLeave(leaveMsg(a.fingerprint(), a), "other").has_value());
    }
}

}  // namespace

int main()
{
    testRosterTrust();
    testTamperedRosterRejected();
    testGroupMessageSenderAuth();
    testGroupLeaveAuth();
    testEmptyGroupApi();
    std::fprintf(stderr, "TestGroup passed\n");
    return 0;
}
