// Bazarish project (c) 2026
#pragma once

#include <bazarish/I2p.hpp>

#include <filesystem>
#include <memory>

namespace bazarish::client {

// The process-wide embedded I2P router. The i2pd engine is process-global, so a
// process may hold only one router; every session shares this single instance
// (the GUI keeps several accounts open at once, and they all route over it -
// blob fetch, calls, and i2p facades). It is created and started lazily on first
// use under dataDir; the first caller's dataDir wins and later arguments are
// ignored. If it exists but was stopped (I2P toggled off), this starts it again.
// Thread-safe. The object lives until process exit; its network is started and
// stopped to match the enable flag.
bazarish::i2p::Router& sharedI2pRouter(const std::filesystem::path& dataDir);

// The shared router if it exists and is currently running, else nullptr - never
// starts it. Lets a read-only caller (the router status view) poll diagnostics
// without forcing a heavyweight startup on its (e.g. the GUI) thread, and reports
// a stopped (disabled) router honestly as not running.
bazarish::i2p::Router* sharedI2pRouterIfRunning();

// A warm, single-use throwaway destination from the process-wide pool kept ready
// while the router runs, or nullptr when none is warm yet (the caller then builds a
// fresh dest cold). Used for the direct federation fetch (card / resolve) so it does
// not pay cold tunnel-build latency. The endpoint is used once and then dropped.
std::shared_ptr<bazarish::i2p::Endpoint> acquireWarmDest();

// Brings the embedded router into line with the current enable flag: starts it
// (creating it under dataDir on first use) when enabled, stops its network when
// disabled. Reads the flag itself, so concurrent toggles converge on the final
// state. Heavyweight (start/stop join engine threads); call off the GUI thread.
void reconcileI2pRouter(const std::filesystem::path& dataDir);

// Process-wide I2P enable flag (default true). When turned off the transport
// treats every i2p facade as unreachable, so the network runs on clearnet
// facades only (and fails explicitly when none is reachable), and the embedded
// router's network is stopped (see reconcileI2pRouter). The flag is consulted at
// request time, so it also takes effect on the next request without rebuilding
// any session.
void setI2pEnabled(bool enabled);
bool i2pEnabled();

// Process-wide full-privacy flag (default false). When on, the transport refuses
// every clearnet facade, so all traffic goes over I2P only; a profile whose
// facades are all clearnet then has nothing reachable and is explicitly offline.
// Consulted at request time, so it takes effect on the next request.
void setFullPrivacy(bool enabled);
bool fullPrivacy();

}  // namespace bazarish::client
