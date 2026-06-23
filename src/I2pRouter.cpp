// Bazarish project (c) 2026
#include "I2pRouter.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace bazarish::client {

namespace {
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

std::atomic<bool> g_i2pEnabled{true};
}  // namespace

bazarish::i2p::Router& sharedI2pRouter(const std::filesystem::path& dataDir)
{
    const std::lock_guard<std::mutex> lock(routerMutex());
    std::unique_ptr<bazarish::i2p::Router>& router = routerSlot();
    if (!router) {
        router = std::make_unique<bazarish::i2p::Router>(
            bazarish::i2p::RouterConfig{dataDir, bazarish::i2p::Role::eClient});
    } else if (!router->running()) {
        router->start();
    }
    return *router;
}

bazarish::i2p::Router* sharedI2pRouterIfRunning()
{
    const std::lock_guard<std::mutex> lock(routerMutex());
    bazarish::i2p::Router* const router = routerSlot().get();
    return (router != nullptr && router->running()) ? router : nullptr;
}

void reconcileI2pRouter(const std::filesystem::path& dataDir)
{
    const std::lock_guard<std::mutex> lock(routerMutex());
    std::unique_ptr<bazarish::i2p::Router>& router = routerSlot();
    if (g_i2pEnabled.load()) {
        if (!router) {
            router = std::make_unique<bazarish::i2p::Router>(
                bazarish::i2p::RouterConfig{dataDir, bazarish::i2p::Role::eClient});
        } else {
            router->start();
        }
    } else if (router) {
        router->stop();
    }
}

void setI2pEnabled(bool enabled)
{
    g_i2pEnabled.store(enabled);
}

bool i2pEnabled()
{
    return g_i2pEnabled.load();
}

}  // namespace bazarish::client
