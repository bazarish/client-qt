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
        "  bazarish-client init <state> <host> <port> <server-fp> [base-path]\n"
        "  bazarish-client subscribe <state> [days]\n"
        "  bazarish-client whoami <state>\n"
        "  bazarish-client alias <state> <name>\n"
        "  bazarish-client invite <state>\n"
        "  bazarish-client request <state> <peer-fp> <text> [peer-host peer-port [base-path]]\n"
        "  bazarish-client add-invite <state> <invite-file> <text>\n"
        "  bazarish-client add-user <state> <alias> <text> [host port [base-path]]\n"
        "  bazarish-client send <state> <peer-fp> <text>\n"
        "  bazarish-client sync <state>\n"
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
    if (args.size() < 5 || args.size() > 6) {
        printUsage();
        return 2;
    }
    ServerEndpoint endpoint;
    endpoint.host = args[2];
    endpoint.port = std::atoi(args[3].c_str());
    endpoint.serverFingerprint = args[4];
    if (args.size() == 6) {
        endpoint.basePath = args[5];
    }
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
    // request <state> <peer-fp> <text> [peer-host peer-port [base-path]]
    if (args.size() == 5 || args.size() < 4 || args.size() > 7) {
        printUsage();
        return 2;
    }
    std::string peerHost;
    int peerPort = 0;
    std::string peerBasePath;
    if (args.size() >= 6) {
        peerHost = args[4];
        peerPort = std::atoi(args[5].c_str());
    }
    if (args.size() == 7) {
        peerBasePath = args[6];
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.sendContactRequest(args[2], args[3], peerHost, peerPort, peerBasePath);
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
    // add-user <state> <alias> <text> [host port [base-path]]
    if (args.size() == 5 || args.size() < 4 || args.size() > 7) {
        printUsage();
        return 2;
    }
    std::string host;
    int port = 0;
    std::string basePath;
    if (args.size() >= 6) {
        host = args[4];
        port = std::atoi(args[5].c_str());
    }
    if (args.size() == 7) {
        basePath = args[6];
    }
    Session session = Session::open(args[1], keyPassphrase());
    session.addByUsername(args[2], args[3], host, port, basePath);
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

int runSync(const std::vector<std::string>& args)
{
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1], keyPassphrase());
    const std::vector<IncomingMessage> messages = session.sync();
    if (messages.empty()) {
        std::printf("(nothing pending)\n");
        return 0;
    }
    for (const IncomingMessage& message : messages) {
        std::printf("[%s] from %s: %s%s\n", message.kind.c_str(),
            message.fromFingerprint.c_str(), message.text.c_str(),
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
