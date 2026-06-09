// Bazarish project (c) 2026
#include "Session.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr const char* kVersion = "0.0.1";

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

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
        "  bazarish-client request <state> <peer-fp> <text> [peer-host peer-port [base-path]]\n"
        "  bazarish-client send <state> <peer-fp> <text>\n"
        "  bazarish-client sync <state>\n"
        "\n"
        "<state> is a directory holding this client's identity and contacts.\n"
        "request bootstraps a contact (E2E-encrypted to the peer's prekey). Give\n"
        "the peer's facade host/port for a cross-server contact; omit it when the\n"
        "peer is on the same facade. send/sync drive the conversation.\n",
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
    const Session session = Session::create(args[1], endpoint);
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
    Session session = Session::open(args[1]);
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
    const Session session = Session::open(args[1]);
    std::printf("fingerprint: %s\nsealing-key: %s\n", session.fingerprint().c_str(),
        session.sealingPublicB64().c_str());
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
    Session session = Session::open(args[1]);
    session.sendContactRequest(args[2], args[3], peerHost, peerPort, peerBasePath);
    std::printf("contact request sent to %s\n", args[2].c_str());
    return 0;
}

int runSend(const std::vector<std::string>& args)
{
    if (args.size() != 4) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1]);
    session.sendMessage(args[2], args[3]);
    std::printf("message sent to %s\n", args[2].c_str());
    return 0;
}

int runSync(const std::vector<std::string>& args)
{
    if (args.size() != 2) {
        printUsage();
        return 2;
    }
    Session session = Session::open(args[1]);
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
        if (command == "request") {
            return runRequest(args);
        }
        if (command == "send") {
            return runSend(args);
        }
        if (command == "sync") {
            return runSync(args);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }

    printUsage();
    return 2;
}
