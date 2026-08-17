// Bazarish project (c) 2026
#include "I2pRouter.hpp"

#include <bazarish/Log.hpp>

#include "WarmDestPool.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>

namespace bazarish::client {

namespace {
// A small, fixed warm pool of single-use throwaway dests (no demand-driven sizing),
// with a small tunnel quantity - enough to cover the two direct fetches that want a
// one-time dest (card + resolve) without paying cold tunnel-build latency.
constexpr std::size_t kWarmPoolSize = 2;
constexpr int kWarmPoolTunnelQuantity = 3;

std::mutex& routerMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unique_ptr<bazarish::i2p::Router>& routerSlot()
{
    static std::unique_ptr<bazarish::i2p::Router> router;
    return router;
}

// Owned in this TU and constructed after routerSlot (its slot is first touched only
// after the router exists), so at process exit it is destroyed first - its warmer
// thread is joined while the router is still alive.
std::unique_ptr<WarmDestPool>& warmPoolSlot()
{
    static std::unique_ptr<WarmDestPool> pool;
    return pool;
}

// Brings the warm pool up alongside a running router. Call under routerMutex.
void ensureWarmPool(bazarish::i2p::Router& router)
{
    std::unique_ptr<WarmDestPool>& pool = warmPoolSlot();
    if (!pool) {
        pool = std::make_unique<WarmDestPool>(
            router, kWarmPoolSize, kWarmPoolTunnelQuantity, bazarish::i2p::Privacy::eMax);
        pool->start();
    }
}

// Tears the warm pool down (joins its warmer) before the router stops. Call under
// routerMutex.
void stopWarmPool()
{
    std::unique_ptr<WarmDestPool>& pool = warmPoolSlot();
    if (pool) {
        pool->stop();
        pool.reset();
    }
}

std::atomic<bool> g_i2pEnabled{true};
// Strict by default: the netDb comes from our own server, not a public host.
std::atomic<bool> g_publicReseedAllowed{false};
// Full privacy mode (default off): when on, the transport refuses every clearnet
// facade, so all traffic runs over I2P (and a profile with no I2P facade is
// explicitly offline). Consulted at request time, like g_i2pEnabled.
std::atomic<bool> g_fullPrivacy{false};
}  // namespace

bool seedRouterOnce(
    const std::filesystem::path& dataDir, const std::function<std::vector<Bytes>()>& fetch)
{
    if (!fetch) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(routerMutex());
    if (routerSlot()) {
        return true;  // the engine has already loaded its netDb
    }
    std::error_code ec;
    const std::filesystem::path netDb = dataDir / "netDb";
    if (std::filesystem::exists(netDb, ec) && !std::filesystem::is_empty(netDb, ec)) {
        return true;
    }
    try {
        const std::size_t written = bazarish::i2p::seedRouterInfos(dataDir, fetch());
        bazarish::log::info("private reseed: {} routers", written);
        return written > 0;
    } catch (const std::exception& error) {
        bazarish::log::info("private reseed unavailable: {}", error.what());
        return false;
    }
}

bazarish::i2p::Router& sharedI2pRouter(const std::filesystem::path& dataDir)
{
    const std::lock_guard<std::mutex> lock(routerMutex());
    std::unique_ptr<bazarish::i2p::Router>& router = routerSlot();
    if (!router) {
        router = std::make_unique<bazarish::i2p::Router>(
            bazarish::i2p::RouterConfig{dataDir, bazarish::i2p::Role::eClient,
                g_publicReseedAllowed.load()});
    } else if (!router->running()) {
        router->start();
    }
    ensureWarmPool(*router);  // keep a couple of throwaway dests warm for direct fetches
    return *router;
}

bazarish::i2p::Router* sharedI2pRouterIfRunning()
{
    const std::lock_guard<std::mutex> lock(routerMutex());
    bazarish::i2p::Router* const router = routerSlot().get();
    return (router != nullptr && router->running()) ? router : nullptr;
}

std::shared_ptr<bazarish::i2p::Endpoint> acquireWarmDest()
{
    const std::lock_guard<std::mutex> lock(routerMutex());
    WarmDestPool* const pool = warmPoolSlot().get();
    return pool != nullptr ? pool->acquire() : nullptr;
}

void reconcileI2pRouter(const std::filesystem::path& dataDir)
{
    const std::lock_guard<std::mutex> lock(routerMutex());
    std::unique_ptr<bazarish::i2p::Router>& router = routerSlot();
    if (g_i2pEnabled.load()) {
        if (!router) {
            router = std::make_unique<bazarish::i2p::Router>(
                bazarish::i2p::RouterConfig{dataDir, bazarish::i2p::Role::eClient,
                g_publicReseedAllowed.load()});
        } else {
            router->start();
        }
        ensureWarmPool(*router);
    } else if (router) {
        stopWarmPool();  // join the warmer before the router's network stops
        router->stop();
    }
}

void setPublicReseedAllowed(bool allowed)
{
    g_publicReseedAllowed.store(allowed);
}

bool publicReseedAllowed()
{
    return g_publicReseedAllowed.load();
}

void setI2pEnabled(bool enabled)
{
    g_i2pEnabled.store(enabled);
}

bool i2pEnabled()
{
    return g_i2pEnabled.load();
}

void setFullPrivacy(bool enabled)
{
    g_fullPrivacy.store(enabled);
}

bool fullPrivacy()
{
    return g_fullPrivacy.load();
}

}  // namespace bazarish::client
