// Bazarish project (c) 2026
#include "FederationFetch.hpp"

#include "I2pRouter.hpp"

#include <bazarish/Crypto.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>

namespace bazarish::client {

namespace {

// Reads a single newline-terminated header line from the stream (the federation
// framing: one JSON object per line). Mirrors the server's reader, including the
// 64 KiB guard against an unbounded line.
std::string readHeaderLine(bazarish::i2p::Stream& stream)
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

FetchOutcome federationFetchOverI2p(bazarish::i2p::Router& router, const std::string& dest,
    const std::string& op, const Bytes& sealed, const bazarish::i2p::Privacy privacy)
{
    // A warm, pre-built throwaway dest from the pool when one is ready (no cold
    // tunnel-build latency), else a fresh one built cold. Either way the dest is
    // single-use and dropped when this call returns (unlinkability); connecting out
    // does not need a published leaseset.
    std::shared_ptr<bazarish::i2p::Endpoint> warm = acquireWarmDest();
    std::shared_ptr<bazarish::i2p::Endpoint> fresh;
    if (!warm) {
        fresh = router.createEndpoint(bazarish::i2p::EndpointConfig{
            bazarish::i2p::Keys::generate(), bazarish::i2p::LeaseSetKind::eEncrypted, privacy,
            bazarish::i2p::kDefaultTunnelQuantity, false, "Contact lookup"});
    }
    bazarish::i2p::Endpoint& endpoint = warm ? *warm : *fresh;
    auto stream = endpoint.connect(dest, std::chrono::seconds(60));
    if (!stream) {
        throw std::runtime_error("federation fetch: cannot reach " + dest);
    }

    const nlohmann::json header = {{"op", op}, {"sealed", toBase64(sealed)}};
    const std::string line = header.dump() + "\n";
    stream->writeAll(line.data(), line.size());

    const nlohmann::json reply = nlohmann::json::parse(readHeaderLine(*stream));
    FetchOutcome outcome;
    outcome.ok = reply.at("ok").get<bool>();
    if (reply.contains("sealed")) {
        outcome.sealed = fromBase64(reply.at("sealed").get<std::string>());
    }
    outcome.errorCode = reply.value("errorCode", std::string());
    return outcome;
}

}  // namespace bazarish::client
