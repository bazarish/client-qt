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

}  // namespace bazarish::client
