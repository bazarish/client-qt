// Bazarish project (c) 2026
#pragma once

#include <bazarish/I2p.hpp>

#include <filesystem>

namespace bazarish::client {

// The process-wide embedded I2P router. The i2pd engine is process-global, so a
// process may hold only one router; every session shares this single instance
// (the GUI keeps several accounts open at once, and they all route over it -
// blob fetch, calls, and i2p facades). It is started lazily on first use under
// dataDir; the first caller's dataDir wins and later arguments are ignored.
// Thread-safe. The router lives until process exit.
bazarish::i2p::Router& sharedI2pRouter(const std::filesystem::path& dataDir);

// The shared router if it has already been started, else nullptr - never starts
// it. Lets a read-only caller (the router status view) poll diagnostics without
// forcing a heavyweight startup on its (e.g. the GUI) thread.
bazarish::i2p::Router* sharedI2pRouterIfRunning();

// Process-wide I2P enable flag (default true). When turned off the transport
// treats every i2p facade as unreachable, so the network runs on clearnet
// facades only (and fails explicitly when none is reachable); the embedded
// router is left untouched. The setting is consulted at request time, so a
// toggle takes effect on the next request without rebuilding any session.
void setI2pEnabled(bool enabled);
bool i2pEnabled();

}  // namespace bazarish::client
