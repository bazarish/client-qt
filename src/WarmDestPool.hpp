// Bazarish project (c) 2026
#pragma once

#include <bazarish/I2p.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace bazarish::client {

// A small pool of pre-built, SINGLE-USE transient outbound I2P destinations kept
// warm (their tunnels already built) so a direct federation fetch - a contact-add
// card fetch or an alias resolve - grabs a ready one instead of paying the cold
// tunnel-build latency (seconds). Each acquired dest is used exactly once and then
// dropped, never reused: two one-time lookups are therefore never linkable to the
// same destination (the same anonymity invariant the throwaway dest exists for).
//
// The client's mirror of the server's warm pool, but deliberately simple: a fixed
// target size (no demand-driven sizing) and a small tunnel quantity. Best-effort -
// a slow or failing build (a router that cannot yet build tunnels) never blocks
// acquire(), which just returns nullptr so the caller builds a fresh dest cold.
class WarmDestPool {
public:
    WarmDestPool(bazarish::i2p::Router& router, std::size_t size, int tunnelQuantity);
    ~WarmDestPool();

    void start();
    void stop();

    // A warm, single-use endpoint, or nullptr when none is ready (the caller then
    // builds a fresh dest cold). The returned endpoint must be used once and dropped
    // - never returned to the pool.
    std::shared_ptr<bazarish::i2p::Endpoint> acquire();

private:
    void warmerLoop();
    // A destination still warming (its tunnels building), with the time it started so
    // a never-ready build can be abandoned.
    struct Building {
        std::shared_ptr<bazarish::i2p::Endpoint> endpoint;
        std::chrono::steady_clock::time_point startedAt;
    };

    bazarish::i2p::Router& router_;
    const std::size_t size_;
    const int tunnelQuantity_;
    // How long a freshly created dest is given to warm before it is abandoned.
    const std::chrono::seconds buildTimeout_{120};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<bazarish::i2p::Endpoint>> ready_;  // warm, unused
    std::atomic<bool> running_{false};
    std::thread warmer_;
};

}  // namespace bazarish::client
