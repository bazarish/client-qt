// Bazarish project (c) 2026
#pragma once

#include "Client.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Sam.hpp>

#include <cstdint>
#include <string>

namespace bazarish::client {

// Speaks one federation fetch frame to a .b32.i2p destination over a FRESH
// transient SAM session (the direct path: our own server is never involved, and
// the throwaway destination keeps the dial unlinkable). The frame mirrors the
// server's federationServeOnce: a single JSON header line {op, sealed} out, a
// single JSON header line {ok, sealed?, errorCode?} back. Used for the card
// fetch (dial the contact's serving server) and the alias resolve (dial the
// central resolver). Construction blocks on tunnel build, so each call carries
// I2P latency. Throws on any transport failure (no SAM bridge, unreachable
// destination, malformed frame) so the caller can fall back to the proxy.
FetchOutcome federationFetchOverSam(const std::string& samHost, std::uint16_t samPort,
    const std::string& dest, const std::string& op, const Bytes& sealed,
    I2pPrivacy privacy = I2pPrivacy::eMax);

}  // namespace bazarish::client
