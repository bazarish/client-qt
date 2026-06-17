// Bazarish project (c) 2026
#include "FederationFetch.hpp"

#include <bazarish/Crypto.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace bazarish::client {

namespace {

// Reads a single newline-terminated header line from the stream (the federation
// framing: one JSON object per line). Mirrors the server's reader, including the
// 64 KiB guard against an unbounded line.
std::string readHeaderLine(SamStream& stream)
{
    std::string line;
    char c = 0;
    while (true) {
        stream.readExact(&c, 1);
        if (c == '\n') {
            break;
        }
        line.push_back(c);
        if (line.size() > 64 * 1024) {
            throw std::runtime_error("federation header line too long");
        }
    }
    return line;
}

}  // namespace

FetchOutcome federationFetchOverSam(const std::string& samHost, const std::uint16_t samPort,
    const std::string& dest, const std::string& op, const Bytes& sealed, const I2pPrivacy privacy)
{
    // A fresh throwaway destination per call (unlinkability). Construction blocks
    // on tunnel build.
    SamSession session(samHost, samPort, "fedfetch-" + toHex(randomBytes(6)), "TRANSIENT",
        kEncryptedLeaseSetType, privacy);
    SamStream stream = session.connect(dest);

    const nlohmann::json header = {{"op", op}, {"sealed", toBase64(sealed)}};
    const std::string line = header.dump() + "\n";
    stream.writeAll(line.data(), line.size());
    // No half-close: the framing is line-delimited both ways, and SAM propagates
    // a SHUT_WR as a full stream teardown.

    const nlohmann::json reply = nlohmann::json::parse(readHeaderLine(stream));
    FetchOutcome outcome;
    outcome.ok = reply.at("ok").get<bool>();
    if (reply.contains("sealed")) {
        outcome.sealed = fromBase64(reply.at("sealed").get<std::string>());
    }
    outcome.errorCode = reply.value("errorCode", std::string());
    return outcome;
}

}  // namespace bazarish::client
