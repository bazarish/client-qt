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
#include <atomic>
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
        "  bazarish-client init <profile> <facade-url> <server-fp>\n"
        "  bazarish-client subscribe <profile> [days]\n"
        "  bazarish-client whoami <profile>\n"
        "  bazarish-client i2p-enable <profile> [keyfile.dat]\n"
        "  bazarish-client i2p-publish <profile>\n"
        "  bazarish-client i2p-cancel <profile>\n"
        "  bazarish-client i2p-status <profile>\n"
        "  bazarish-client sign-login <profile> <challenge>\n"
        "  bazarish-client invite <profile>\n"
        "  bazarish-client request <profile> <peer-fp> <text>\n"
        "  bazarish-client add-invite <profile> <invite-file> <text>\n"
        "  bazarish-client add-user <profile> <alias> <text>\n"
        "  bazarish-client alias-cert <profile> <alias>\n"
        "  bazarish-client send <profile> <peer-fp> <text>\n"
        "  bazarish-client send-file <profile> <peer-fp> <file>\n"
        "  bazarish-client send-command <profile> <peer-fp> <command> [args]\n"
        "  bazarish-client send-callback <profile> <peer-fp> <data> [ref]\n"
        "  bazarish-client call <profile> <peer-fp> [seconds]\n"
        "  bazarish-client call-answer <profile> [seconds]\n"
        "  bazarish-client get-file <profile> <peer-fp> <message-id> <out>\n"
        "  bazarish-client unsend <profile> <message-id>\n"
        "  bazarish-client sync <profile> [--privacy <minimal|middle|max>]\n"
        "  bazarish-client export <profile> <out-file>\n"
        "  bazarish-client import <in-file> <profile>\n"
        "\n"
        "<profile> is a directory holding this client's identity and contacts.\n"
        "request/add-* bootstrap a contact (E2E-encrypted to the peer's prekey).\n"
        "invite prints a self-verifying bazarish:// link and QR codes carrying the\n"
        "full trust chain (no server trust needed). add-invite consumes such a\n"
        "link; add-user resolves a username (trusts the resolver for the mapping).\n"
        "\n"
        "Environment:\n"
        "  BAZARISH_PASSPHRASE       encrypts/decrypts the key PEMs at rest\n"
        "  BAZARISH_EXPORT_PASSWORD  protects the export/import bundle (required)\n"
        "  BAZARISH_RESOLVER_ROOT    central resolver root fingerprint (overrides built-in)\n"
        "  BAZARISH_RESOLVER_DEST    central resolver .b32.i2p destination\n"
        "  BAZARISH_RESOLVER_KEY     central resolver serving key (base64 SPKI DER)\n",
        kVersion);
}

int runInit(const std::vector<std::string>& args)
{
    // init <profile> <facade-url> <server-fp>
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
    // i2p-enable <profile> [keyfile.dat]: set up a user-owned I2P destination.
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

int runI2pPublish(const std::vector<std::string>& args)
{
    // i2p-publish <profile>: delegate a fresh transient and re-issue the contact
    // card with the routing in it. Subscribing does this already; it is needed
    // again only after a moderated server approves the account.
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.publishRouting();
    std::printf("routing published: %s.b32.i2p\n", session.i2pAddress().c_str());
    return 0;
}

int runI2pCancel(const std::vector<std::string>& args)
{
    // i2p-cancel <profile>: revoke the destination server-side. The master stays
    // in the profile, so i2p-publish later restores the same address.
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.disableI2pDest();
    std::printf("I2P destination revoked; the profile keeps its master key\n");
    return 0;
}

int runI2pStatus(const std::vector<std::string>& args)
{
    // i2p-status <profile>: print the per-user i2p-dest status from the server.
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const bazarish::client::I2pDestStatus s = session.i2pDestStatus();
    std::printf("address: %s\napproval: %s\ntransientExpires: %lld\n"
                "transientUpdatedAt: %lld\n",
        session.i2pAddress().empty() ? "(none)" : (session.i2pAddress() + ".b32.i2p").c_str(),
        s.approval.empty() ? "(unknown)" : s.approval.c_str(),
        static_cast<long long>(s.transientExpires),
        static_cast<long long>(s.transientUpdatedAt));
    if (!s.registrationMessage.empty()) {
        std::printf("message: %s\n", s.registrationMessage.c_str());
    }
    return 0;
}

int runSignLogin(const std::vector<std::string>& args)
{
    // sign-login <profile> <challenge>: prove key ownership to a service portal by
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
    // request <profile> <peer-fp> <text>  (peer must be on our own server;
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
    // add-invite <profile> <invite-file> <text>
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
    // add-user <profile> <alias> <text>  (alias resolves on the central resolver
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
    // alias-cert <profile> <alias>: emit (as JSON) the signed artifacts the central
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
    // send-file <profile> <peer-fp> <file>
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
    // send-command <profile> <peer-fp> <command> [args]
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
    // send-callback <profile> <peer-fp> <data> [ref]
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
    // get-file <profile> <peer-fp> <message-id> <out>
    if (args.size() != 5) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());

    // The transfer is asynchronous by nature - it needs the sender online and the
    // offer to come back through a sync - so drive syncs until it settles.
    std::atomic<bool> finished{false};
    std::atomic<bool> ok{false};
    std::string failure;
    session.setTransferHandler([&](const bazarish::client::TransferEvent& event) {
        if (event.state == bazarish::client::TransferState::eRunning && event.total > 0) {
            std::printf("\r%llu / %llu bytes", static_cast<unsigned long long>(event.bytes),
                static_cast<unsigned long long>(event.total));
            std::fflush(stdout);
        } else if (event.state == bazarish::client::TransferState::eDone) {
            ok = true;
            finished = true;
        } else if (event.state == bazarish::client::TransferState::eFailed) {
            failure = event.error;
            finished = true;
        }
    });
    session.requestFile(args[2], args[3], args[4]);
    for (int i = 0; i < 600 && !finished.load(); ++i) {
        (void)session.sync();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!ok.load()) {
        std::fprintf(stderr, "\ntransfer failed: %s\n",
            failure.empty() ? "timed out waiting for the sender" : failure.c_str());
        return 1;
    }
    std::printf("\nsaved attachment to %s\n", args[4].c_str());
    return 0;
}

int runUnsend(const std::vector<std::string>& args)
{
    // unsend <profile> <message-id>
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
    // export <profile> <out-file>
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
    session.exportProfile(args[2], password);
    std::printf("exported encrypted session to %s\n", args[2].c_str());
    return 0;
}

int runImport(const std::vector<std::string>& args)
{
    // import <in-file> <profile>
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
    Session::importProfile(args[1], args[2], password, keyPassphrase());
    std::printf("imported session into %s\n", args[2].c_str());
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
        const std::optional<bazarish::i2p::Privacy> parsed = bazarish::i2p::privacyFromString(args[3]);
        if (!parsed.has_value()) {
            printUsage();
            return 2;
        }
        session.setTransferPrivacy(parsed.value());
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
        } else if (!message.attachmentName.empty()) {
            // An announcement: `get-file` pulls it straight from the sender.
            body = message.attachmentName + " (" + std::to_string(message.attachmentSize)
                + " bytes) id=" + message.messageId;
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
        // The message id lets a caller reference this message (e.g. as the ref
        // of a send-callback, so a bot can edit it in place).
        std::printf("[%s] from %s: %s  id=%s%s\n",
            message.contentType.c_str(), message.fromFingerprint.c_str(), body.c_str(),
            message.messageId.c_str(),
            message.establishedContact ? "  (contact established)" : "");
    }
    return 0;
}

// Places an outgoing audio call and drives the signalling sync loop until the
// peer answers, then runs media for the remaining window. STRICT I2P: without a
// working transport startAudioCall throws a readable error and nothing is dialled.
int runCall(const std::vector<std::string>& args)
{
    // call <profile-dir> <peer-fp> [seconds]
    if (args.size() < 3 || args.size() > 4) {
        printUsage();
        return 2;
    }
    int seconds = 30;
    for (std::size_t i = 3; i < args.size(); ++i) {
        seconds = std::atoi(args[i].c_str());
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.startAudioCall(args[2]);
    std::printf("calling %s (audio)...\n", args[2].c_str());
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
// runs media for the window, then ends. STRICT I2P applies on accept.
int runCallAnswer(const std::vector<std::string>& args)
{
    // call-answer <profile-dir> [seconds]
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
            std::printf("incoming call from %s; accepting\n",
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
        if (command == "i2p-publish") {
            return runI2pPublish(args);
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
