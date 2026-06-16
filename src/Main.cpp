// Bazarish project (c) 2026
#include "Qr.hpp"
#include "Session.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>
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
        "  bazarish-client i2p-enable <state>\n"
        "  bazarish-client sign-login <state> <challenge>\n"
        "  bazarish-client alias <state> <name>\n"
        "  bazarish-client invite <state>\n"
        "  bazarish-client request <state> <peer-fp> <text>\n"
        "  bazarish-client add-invite <state> <invite-file> <text>\n"
        "  bazarish-client add-user <state> <alias> <text>\n"
        "  bazarish-client send <state> <peer-fp> <text>\n"
        "  bazarish-client send-file <state> <peer-fp> <file>\n"
        "  bazarish-client send-command <state> <peer-fp> <command> [args]\n"
        "  bazarish-client send-callback <state> <peer-fp> <data> [ref]\n"
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
        "  BAZARISH_EXPORT_PASSWORD  protects the export/import bundle (required)\n",
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
    // i2p-enable <state>: mint a user-owned I2P destination (the per-user
    // portable address). Idempotent; the master key never leaves the client.
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const std::string address = session.ensureI2pDestination();
    std::printf("user-owned I2P destination: %s.b32.i2p\n", address.c_str());
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

int runAlias(const std::vector<std::string>& args)
{
    // alias <state> <name>
    if (args.size() != 3) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.registerAlias(args[2]);
    std::printf("registered alias %s\n", args[2].c_str());
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
    // cross-server first contact uses add-invite — facade locality)
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
    session.addByInvite(trimmed, args[3]);
    std::printf("contact request sent from invite\n");
    return 0;
}

int runAddUser(const std::vector<std::string>& args)
{
    // add-user <state> <alias> <text>  (alias resolves on our own server;
    // facade locality — no foreign resolver)
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.addByUsername(args[2], args[3]);
    std::printf("contact request sent to %s (resolver-trusted mapping)\n", args[2].c_str());
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
        std::fprintf(stderr, "error: set BAZARISH_EXPORT_PASSWORD\n");
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
        std::fprintf(stderr, "error: set BAZARISH_EXPORT_PASSWORD\n");
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
    if (messages.empty()) {
        std::printf("(nothing pending)\n");
        return 0;
    }
    for (const IncomingMessage& message : messages) {
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

}  // namespace

int main(const int argc, const char** argv)
{
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
        if (command == "sign-login") {
            return runSignLogin(args);
        }
        if (command == "alias") {
            return runAlias(args);
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
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }

    printUsage();
    return 2;
}
