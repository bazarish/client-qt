// Bazarish project (c) 2026
#include "I2pRouter.hpp"

#include <memory>
#include <mutex>

namespace bazarish::client {

bazarish::i2p::Router& sharedI2pRouter(const std::filesystem::path& dataDir)
{
    static std::mutex mutex;
    static std::unique_ptr<bazarish::i2p::Router> router;
    const std::lock_guard<std::mutex> lock(mutex);
    if (!router) {
        router = std::make_unique<bazarish::i2p::Router>(
            bazarish::i2p::RouterConfig{dataDir, bazarish::i2p::Role::eClient});
    }
    return *router;
}

}  // namespace bazarish::client
