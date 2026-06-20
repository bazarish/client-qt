// Bazarish project (c) 2026
#include "Qr.hpp"
#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Log.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kVersion = "0.0.1";

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

// The at-rest passphrase for the key PEMs, taken from the environment so the
// CLI stays non-interactive and the passphrase never reaches the process
// argument list. Empty means unencrypted keys.
std::string keyPassphrase()
{
    const char* const value = std::getenv("BAZARISH_PASSPHRASE");
    return value != nullptr ? std::string(value) : std::string();
}

// The password protecting an exported bundle (a separate secret from the
// at-rest passphrase).
std::string exportPassword()
{
    const char* const value = std::getenv("BAZARISH_EXPORT_PASSWORD");
    return value != nullptr ? std::string(value) : std::string();
}

std::string readWholeFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open " + path);
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void printUsage()
{
    std::printf(
        "bazarish-client %s\n"
        "A stateful command-line messenger client.\n"
        "\n"
        "Usage:\n"
        "  bazarish-client init <state> <facade-url> <server-fp>\n"
        "  bazarish-client subscribe <state> [days]\n"
        "  bazarish-client whoami <state>\n"
        "  bazarish-client i2p-enable <state> [keyfile.dat]\n"
        "  bazarish-client i2p-buy <state>\n"
        "  bazarish-client i2p-cancel <state>\n"
        "  bazarish-client i2p-status <state>\n"
        "  bazarish-client sign-login <state> <challenge>\n"
        "  bazarish-client invite <state>\n"
        "  bazarish-client request <state> <peer-fp> <text>\n"
        "  bazarish-client add-invite <state> <invite-file> <text>\n"
        "  bazarish-client add-user <state> <alias> <text>\n"
        "  bazarish-client alias-cert <state> <alias>\n"
        "  bazarish-client send <state> <peer-fp> <text>\n"
        "  bazarish-client send-file <state> <peer-fp> <file>\n"
        "  bazarish-client send-command <state> <peer-fp> <command> [args]\n"
        "  bazarish-client send-callback <state> <peer-fp> <data> [ref]\n"
        "  bazarish-client call <state> <peer-fp> [seconds] [video]\n"
        "  bazarish-client call-answer <state> [seconds]\n"
        "  bazarish-client get-file <state> <ref> <key-b64> <out>\n"
        "  bazarish-client group-create <state> <name> <peer-fp> [peer-fp ...]\n"
        "  bazarish-client group-send <state> <group-id> <text>\n"
        "  bazarish-client group-list <state>\n"
        "  bazarish-client group-members <state> <group-id>\n"
        "  bazarish-client group-add <state> <group-id> <peer-fp> [peer-fp ...]\n"
        "  bazarish-client group-remove <state> <group-id> <peer-fp>\n"
        "  bazarish-client group-admin <state> <group-id> <peer-fp> <on|off>\n"
        "  bazarish-client group-leave <state> <group-id>\n"
        "  bazarish-client unsend <state> <message-id>\n"
        "  bazarish-client sync <state> [--privacy <minimal|middle|max>]\n"
        "  bazarish-client export <state> <out-file>\n"
        "  bazarish-client import <in-file> <state>\n"
        "\n"
        "<state> is a directory holding this client's identity and contacts.\n"
        "request/add-* bootstrap a contact (E2E-encrypted to the peer's prekey).\n"
        "invite prints a self-verifying bazarish:// link and QR codes carrying the\n"
        "full trust chain (no server trust needed). add-invite consumes such a\n"
        "link; add-user resolves a username (trusts the resolver for the mapping).\n"
        "\n"
        "Environment:\n"
        "  BAZARISH_PASSPHRASE       encrypts/decrypts the key PEMs at rest\n"
        "  BAZARISH_EXPORT_PASSWORD  protects the export/import bundle (required)\n"
        "  BAZARISH_SAM_PORT         local SAM API port (loopback host; default 7656)\n"
        "  BAZARISH_RESOLVER_ROOT    central resolver root fingerprint (overrides built-in)\n"
        "  BAZARISH_RESOLVER_DEST    central resolver .b32.i2p destination\n"
        "  BAZARISH_RESOLVER_KEY     central resolver serving key (base64 SPKI DER)\n",
        kVersion);
}

int runInit(const std::vector<std::string>& args)
{
    // init <state> <facade-url> <server-fp>
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    ServerEndpoint endpoint;
    endpoint.facades = {bazarish::client::parseFacadeUrl(args[2])};
    endpoint.serverFingerprint = args[3];
    const Session session = Session::create(args[1], endpoint, keyPassphrase());
    std::printf("created client\nfingerprint: %s\n", session.fingerprint().c_str());
    return 0;
}

int runSubscribe(const std::vector<std::string>& args)
{
    if (args.size() < 2 || args.size() > 3) {
        printUsage();
        return 2;
    }
    const std::int64_t days = args.size() == 3 ? std::atoll(args[2].c_str()) : 14;
    Session session = Session::open(args[1], keyPassphrase());
    session.subscribe(days);
    std::printf("subscribed for %lld day(s); client registered\n",
        static_cast<long long>(days));
    if (session.hasI2pDestination()) {
        std::printf("issued I2P transient delegation (%zu bytes) for %s.b32.i2p\n",
            session.i2pTransient().size(), session.i2pAddress().c_str());
    }
    return 0;
}

int runWhoami(const std::vector<std::string>& args)
{
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    const Session session = Session::open(args[1], keyPassphrase());
    std::printf("fingerprint: %s\nsealing-key: %s\n", session.fingerprint().c_str(),
        session.sealingPublicB64().c_str());
    if (session.hasI2pDestination()) {
        std::printf("i2p-address: %s.b32.i2p\n", session.i2pAddress().c_str());
    }
    return 0;
}

int runI2pEnable(const std::vector<std::string>& args)
{
    // i2p-enable <state> [keyfile.dat]: set up a user-owned I2P destination.
    // With no file it mints a fresh random master; with a .dat it adopts an
    // existing unencrypted Ed25519 destination key. The master never leaves the
    // client.
    if (args.size() < 2 || args.size() > 3) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    std::string address;
    if (args.size() == 3) {
        std::ifstream in(args[2], std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot open key file: %s\n", args[2].c_str());
            return 1;
        }
        const bazarish::Bytes dat(
            (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        address = session.loadI2pDestination(dat);
    } else {
        address = session.ensureI2pDestination();
    }
    std::printf("user-owned I2P destination: %s.b32.i2p\n", address.c_str());
    return 0;
}

int runI2pBuy(const std::vector<std::string>& args)
{
    // i2p-buy <state>: turn on the paid per-user destination (charges a term,
    // issues and uploads a transient, and backs the master up to other devices).
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    if (!session.enableI2pDest(static_cast<std::int64_t>(std::time(nullptr)))) {
        std::fprintf(stderr, "could not enable: insufficient balance (top up on the portal)\n");
        return 1;
    }
    std::printf("personal I2P destination enabled: %s.b32.i2p\n", session.i2pAddress().c_str());
    return 0;
}

int runI2pCancel(const std::vector<std::string>& args)
{
    // i2p-cancel <state>: turn the paid per-user destination off (falls back to
    // the fixed pool address). The master stays in the profile.
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.disableI2pDest();
    std::printf("personal I2P destination disabled; back on the shared pool address\n");
    return 0;
}

int runI2pStatus(const std::vector<std::string>& args)
{
    // i2p-status <state>: print the per-user i2p-dest status from the server.
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const bazarish::client::I2pDestStatus s = session.i2pDestStatus();
    std::printf("enabled: %s\nactive: %s\npaidThrough: %lld\nprojectedShutoff: %lld\n"
                "transientExpires: %lld\nstorageQuotaBytes: %llu\nstorageActive: %s\n"
                "storageProjectedShutoff: %lld\nbalance: %s %s\n",
        s.enabled ? "yes" : "no", s.active ? "yes" : "no",
        static_cast<long long>(s.paidThrough), static_cast<long long>(s.projectedShutoff),
        static_cast<long long>(s.transientExpires),
        static_cast<unsigned long long>(s.storageQuotaBytes), s.storageActive ? "yes" : "no",
        static_cast<long long>(s.storageProjectedShutoff), s.balanceAtomic.c_str(),
        s.currency.c_str());
    return 0;
}

int runSignLogin(const std::vector<std::string>& args)
{
    // sign-login <state> <challenge>: prove key ownership to a service portal by
    // signing its challenge; print the blob to paste back into the site.
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    const Session session = Session::open(args[1], keyPassphrase());
    std::printf("%s\n", session.signLogin(args[2]).c_str());
    return 0;
}

int runInvite(const std::vector<std::string>& args)
{
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    const Session session = Session::open(args[1], keyPassphrase());
    const std::string uri = session.inviteUri();
    std::printf("%s\n\n", uri.c_str());

    const std::vector<std::string> codes = bazarish::client::renderQrCodes(uri);
    for (std::size_t i = 0; i < codes.size(); ++i) {
        std::printf("--- QR %zu/%zu ---\n%s\n", i + 1, codes.size(), codes[i].c_str());
    }
    return 0;
}

int runRequest(const std::vector<std::string>& args)
{
    // request <state> <peer-fp> <text>  (peer must be on our own server;
    // cross-server first contact uses add-invite - facade locality)
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.sendContactRequest(args[2], args[3]);
    std::printf("contact request sent to %s\n", args[2].c_str());
    return 0;
}

int runAddInvite(const std::vector<std::string>& args)
{
    // add-invite <state> <invite-file> <text>
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    const std::string uri = readWholeFile(args[2]);
    Session session = Session::open(args[1], keyPassphrase());
    // Trim trailing whitespace/newline the file may carry.
    std::string trimmed = uri;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r'
                                   || trimmed.back() == ' ')) {
        trimmed.pop_back();
    }
    const std::string fingerprint = session.addByInvite(trimmed, args[3]);
    // Surface the fingerprint for out-of-band verification (the descriptor is the
    // integrity anchor; the card was verified against it).
    std::printf("contact request sent from invite (verify fingerprint: %s)\n", fingerprint.c_str());
    return 0;
}

int runAddUser(const std::vector<std::string>& args)
{
    // add-user <state> <alias> <text>  (alias resolves on the central resolver
    // over a signed, self-verifying record)
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const std::string fingerprint = session.addByUsername(args[2], args[3]);
    // The alias->fingerprint binding is the one residual trust of the name path;
    // print the resolved fingerprint so the user can verify it out of band.
    std::printf("contact request sent to %s (verify fingerprint: %s)\n", args[2].c_str(),
        fingerprint.c_str());
    return 0;
}

int runAliasCert(const std::vector<std::string>& args)
{
    // alias-cert <state> <alias>: emit (as JSON) the signed artifacts the central
    // resolver's portal needs to claim a name for this identity - the alias, the
    // user's serving destination + sealing key, and a user-signed alias
    // certificate. POST it to the resolver's /portal/buy (the key never leaves the
    // client; the portal only verifies the signature).
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    const Session session = Session::open(args[1], keyPassphrase());
    std::printf("%s\n", session.aliasBuyArtifacts(args[2]).c_str());
    return 0;
}

int runSend(const std::vector<std::string>& args)
{
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.sendMessage(args[2], args[3]);
    std::printf("message sent to %s\n", args[2].c_str());
    return 0;
}

int runSendFile(const std::vector<std::string>& args)
{
    // send-file <state> <peer-fp> <file>
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.sendFile(args[2], args[3]);
    std::printf("file sent to %s\n", args[2].c_str());
    return 0;
}

int runSendCommand(const std::vector<std::string>& args)
{
    // send-command <state> <peer-fp> <command> [args]
    if (args.size() < 4 || args.size() > 5) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const std::string commandArgs = args.size() == 5 ? args[4] : std::string();
    session.sendCommand(args[2], args[3], commandArgs);
    std::printf("command /%s sent to %s\n", args[3].c_str(), args[2].c_str());
    return 0;
}

int runSendCallback(const std::vector<std::string>& args)
{
    // send-callback <state> <peer-fp> <data> [ref]
    if (args.size() < 4 || args.size() > 5) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const std::string ref = args.size() == 5 ? args[4] : std::string();
    session.sendCallback(args[2], args[3], ref);
    std::printf("callback '%s' sent to %s\n", args[3].c_str(), args[2].c_str());
    return 0;
}

int runGetFile(const std::vector<std::string>& args)
{
    // get-file <state> <ref> <key-b64> <out>
    if (args.size() != 5) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.saveAttachment(args[2], args[3], args[4]);
    std::printf("saved attachment to %s\n", args[4].c_str());
    return 0;
}

int runUnsend(const std::vector<std::string>& args)
{
    // unsend <state> <message-id>
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.unsend(args[2]);
    std::printf("unsent the blob for message %s\n", args[2].c_str());
    return 0;
}

int runExport(const std::vector<std::string>& args)
{
    // export <state> <out-file>
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    const std::string password = exportPassword();
    if (password.empty()) {
        bazarish::log::error("set BAZARISH_EXPORT_PASSWORD");
        return 1;
    }
    const Session session = Session::open(args[1], keyPassphrase());
    session.exportState(args[2], password);
    std::printf("exported encrypted session to %s\n", args[2].c_str());
    return 0;
}

int runImport(const std::vector<std::string>& args)
{
    // import <in-file> <state>
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    const std::string password = exportPassword();
    if (password.empty()) {
        bazarish::log::error("set BAZARISH_EXPORT_PASSWORD");
        return 1;
    }
    // The imported keys adopt the at-rest passphrase (if any) of this host.
    Session::importState(args[1], args[2], password, keyPassphrase());
    std::printf("imported session into %s\n", args[2].c_str());
    return 0;
}

int runGroupCreate(const std::vector<std::string>& args)
{
    // group-create <state> <name> <peer-fp> [peer-fp ...]
    if (args.size() < 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const std::vector<std::string> members(args.begin() + 3, args.end());
    const std::string groupId = session.createGroup(args[2], members);
    std::printf("created group '%s'\ngroup id: %s\nmembers: %zu\n", args[2].c_str(),
        groupId.c_str(), members.size());
    return 0;
}

int runGroupSend(const std::vector<std::string>& args)
{
    // group-send <state> <group-id> <text>
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.sendGroupMessage(args[2], args[3]);
    std::printf("group message sent to %s\n", args[2].c_str());
    return 0;
}

int runGroupList(const std::vector<std::string>& args)
{
    // group-list <state>
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    const Session session = Session::open(args[1], keyPassphrase());
    const std::vector<std::string> ids = session.groupIds();
    if (ids.empty()) {
        std::printf("(no groups)\n");
        return 0;
    }
    for (const std::string& id : ids) {
        std::printf("%s  %s  (%zu members)\n", id.c_str(), session.groupName(id).c_str(),
            session.groupMemberFingerprints(id).size());
    }
    return 0;
}

int runGroupMembers(const std::vector<std::string>& args)
{
    // group-members <state> <group-id>
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    const Session session = Session::open(args[1], keyPassphrase());
    for (const std::string& fp : session.groupMemberFingerprints(args[2])) {
        std::printf("%s\n", fp.c_str());
    }
    return 0;
}

int runGroupAdd(const std::vector<std::string>& args)
{
    // group-add <state> <group-id> <peer-fp> [peer-fp ...]
    if (args.size() < 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const std::vector<std::string> members(args.begin() + 3, args.end());
    session.addGroupMembers(args[2], members);
    std::printf("added %zu member(s) to group %s\n", members.size(), args[2].c_str());
    return 0;
}

int runGroupRemove(const std::vector<std::string>& args)
{
    // group-remove <state> <group-id> <peer-fp>
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.removeGroupMember(args[2], args[3]);
    std::printf("removed %s from group %s (pool rotated)\n", args[3].c_str(), args[2].c_str());
    return 0;
}

int runGroupAdmin(const std::vector<std::string>& args)
{
    // group-admin <state> <group-id> <peer-fp> <on|off>
    if (args.size() != 5) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const bool on = args[4] == "on";
    session.setGroupAdmin(args[2], args[3], on);
    std::printf("%s admin for %s in group %s\n", on ? "granted" : "revoked", args[3].c_str(),
        args[2].c_str());
    return 0;
}

int runGroupLeave(const std::vector<std::string>& args)
{
    // group-leave <state> <group-id>
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.leaveGroup(args[2]);
    std::printf("left group %s\n", args[2].c_str());
    return 0;
}

int runSync(const std::vector<std::string>& args)
{
    if (args.size() != 2 && !(args.size() == 4 && args[2] == "--privacy")) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    if (args.size() == 4) {
        const std::optional<bazarish::I2pPrivacy> parsed = bazarish::i2pPrivacyFromString(args[3]);
        if (!parsed.has_value()) {
            printUsage();
            return 2;
        }
        session.setBlobFetchPrivacy(parsed.value());
    }
    const std::vector<IncomingMessage> messages = session.sync();
    // Keep a personal destination's transient fresh (a no-op for free profiles).
    // Re-issue ~2 days before the 7-day transient lapses, with a few hours of
    // per-device jitter so concurrent devices do not all issue at once; the
    // poll-before-issue inside stands the losers down.
    try {
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t jitter
            = static_cast<std::int64_t>(bazarish::randomBytes(1)[0]) * 6 * 3600 / 255;
        session.refreshI2pTransientIfDue(now, 5 * 24 * 3600 - jitter);
    } catch (const std::exception&) {
    }
    if (messages.empty()) {
        std::printf("(nothing pending)\n");
        return 0;
    }
    for (const IncomingMessage& message : messages) {
        // A device self-sync (the I2P master backup) is applied silently inside
        // sync(); do not print it as a chat message.
        if (message.contentType == "device.i2p-master") {
            continue;
        }
        std::string body;
        if (message.contentType == "unsupported") {
            body = std::string("(unsupported type '") + message.rawType
                + "' — update your app)";
        } else if (!message.attachmentRef.empty()) {
            // Print the reference and key so `get-file` can download it.
            body = message.attachmentName + " (" + std::to_string(message.attachmentSize)
                + " bytes) ref=" + message.attachmentRef + " key=" + message.attachmentKeyB64;
        } else if (message.contentType == "bot.callback") {
            body = "data=" + message.callbackData + " ref=" + message.refId;
        } else {
            body = message.text;
        }
        // Show any inline keyboard so a button's data/command is visible to a
        // human driving the CLI (then replied to with send-callback/-command).
        if (!message.keyboardJson.empty()) {
            body += "  keyboard=" + message.keyboardJson;
        }
        // Group messages are filed under a group rather than the 1:1 thread.
        const std::string groupTag = message.groupId.empty()
            ? std::string()
            : "[group " + message.groupId.substr(0, 8) + "] ";
        // The message id lets a caller reference this message (e.g. as the ref
        // of a send-callback, so a bot can edit it in place).
        std::printf("%s[%s] from %s: %s  id=%s%s\n", groupTag.c_str(),
            message.contentType.c_str(), message.fromFingerprint.c_str(), body.c_str(),
            message.messageId.c_str(),
            message.establishedContact ? "  (contact established)" : "");
    }
    return 0;
}

// Places an outgoing audio call and drives the signalling sync loop until the
// peer answers, then runs media for the remaining window. STRICT SAM: without a
// local bridge startAudioCall throws a readable error and nothing is dialled.
int runCall(const std::vector<std::string>& args)
{
    // call <state-dir> <peer-fp> [seconds] [video]
    if (args.size() < 3 || args.size() > 5) {
        printUsage();
        return 2;
    }
    int seconds = 30;
    bool video = false;
    for (std::size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "video") {
            video = true;
        } else {
            seconds = std::atoi(args[i].c_str());
        }
    }
    Session session = Session::open(args[1], keyPassphrase());
    if (video) {
        session.startVideoCall(args[2]);
    } else {
        session.startAudioCall(args[2]);
    }
    std::printf("calling %s (%s)...\n", args[2].c_str(), video ? "video" : "audio");
    const std::int64_t deadline = static_cast<std::int64_t>(std::time(nullptr)) + seconds;
    bool connected = false;
    while (static_cast<std::int64_t>(std::time(nullptr)) < deadline) {
        session.sync();
        const Session::CallInfo call = session.currentCall();
        if (call.state == Session::CallState::eIdle) {
            std::printf("call ended by peer\n");
            return 0;
        }
        if (call.state == Session::CallState::eActive && !connected) {
            std::printf("connected to %s\n", call.peerFingerprint.c_str());
            connected = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    const Session::CallInfo call = session.currentCall();
    std::printf("ending call (media sent=%llu received=%llu)\n",
        static_cast<unsigned long long>(call.packetsSent),
        static_cast<unsigned long long>(call.packetsReceived));
    session.endCall();
    return 0;
}

// Waits for an incoming call.invite, auto-accepts it (a test/headless driver),
// runs media for the window, then ends. STRICT SAM applies on accept.
int runCallAnswer(const std::vector<std::string>& args)
{
    // call-answer <state-dir> [seconds]
    if (args.size() < 2 || args.size() > 3) {
        printUsage();
        return 2;
    }
    const int seconds = args.size() == 3 ? std::atoi(args[2].c_str()) : 60;
    Session session = Session::open(args[1], keyPassphrase());
    std::printf("waiting for an incoming call...\n");
    const std::int64_t deadline = static_cast<std::int64_t>(std::time(nullptr)) + seconds;
    bool accepted = false;
    while (static_cast<std::int64_t>(std::time(nullptr)) < deadline) {
        session.sync();
        const Session::CallInfo call = session.currentCall();
        if (!accepted && call.state == Session::CallState::eIncoming) {
            std::printf("incoming %s call from %s; accepting\n", call.video ? "video" : "audio",
                call.peerFingerprint.c_str());
            session.acceptCall(call.callId);
            accepted = true;
        }
        if (accepted && call.state == Session::CallState::eIdle) {
            std::printf("call ended by peer\n");
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!accepted) {
        std::printf("no incoming call\n");
        return 0;
    }
    const Session::CallInfo call = session.currentCall();
    std::printf("ending call (media sent=%llu received=%llu)\n",
        static_cast<unsigned long long>(call.packetsSent),
        static_cast<unsigned long long>(call.packetsReceived));
    session.endCall();
    return 0;
}

}  // namespace

int main(const int argc, const char** argv)
{
    bazarish::log::setComponent("client");
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("bazarish-client %s\n", kVersion);
        return 0;
    }
    if (argc < 2) {
        printUsage();
        return 2;
    }

    // args[0] is the command; args[1..] are its positional parameters.
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    const std::string& command = args[0];

    try {
        if (command == "init") {
            return runInit(args);
        }
        if (command == "subscribe") {
            return runSubscribe(args);
        }
        if (command == "whoami") {
            return runWhoami(args);
        }
        if (command == "i2p-enable") {
            return runI2pEnable(args);
        }
        if (command == "i2p-buy") {
            return runI2pBuy(args);
        }
        if (command == "i2p-cancel") {
            return runI2pCancel(args);
        }
        if (command == "i2p-status") {
            return runI2pStatus(args);
        }
        if (command == "sign-login") {
            return runSignLogin(args);
        }
        if (command == "invite") {
            return runInvite(args);
        }
        if (command == "request") {
            return runRequest(args);
        }
        if (command == "add-invite") {
            return runAddInvite(args);
        }
        if (command == "add-user") {
            return runAddUser(args);
        }
        if (command == "alias-cert") {
            return runAliasCert(args);
        }
        if (command == "send") {
            return runSend(args);
        }
        if (command == "send-file") {
            return runSendFile(args);
        }
        if (command == "send-command") {
            return runSendCommand(args);
        }
        if (command == "send-callback") {
            return runSendCallback(args);
        }
        if (command == "call") {
            return runCall(args);
        }
        if (command == "call-answer") {
            return runCallAnswer(args);
        }
        if (command == "get-file") {
            return runGetFile(args);
        }
        if (command == "unsend") {
            return runUnsend(args);
        }
        if (command == "group-create") {
            return runGroupCreate(args);
        }
        if (command == "group-send") {
            return runGroupSend(args);
        }
        if (command == "group-list") {
            return runGroupList(args);
        }
        if (command == "group-members") {
            return runGroupMembers(args);
        }
        if (command == "group-add") {
            return runGroupAdd(args);
        }
        if (command == "group-remove") {
            return runGroupRemove(args);
        }
        if (command == "group-admin") {
            return runGroupAdmin(args);
        }
        if (command == "group-leave") {
            return runGroupLeave(args);
        }
        if (command == "sync") {
            return runSync(args);
        }
        if (command == "export") {
            return runExport(args);
        }
        if (command == "import") {
            return runImport(args);
        }
    } catch (const std::exception& error) {
        bazarish::log::error("{}", error.what());
        return 1;
    }

    printUsage();
    return 2;
}
