// Bazarish project (c) 2026
#pragma once

#include <QMetaObject>
#include <QObject>

#include <atomic>
#include <functional>
#include <mutex>

namespace bazarish::app {

// The guard is held from reading the target to posting; false only when Qt refused the post.
template <typename Target>
bool postIfHeld(
    std::mutex& guard, const std::atomic<Target*>& held, std::function<void(Target&)> action)
{
    const std::lock_guard<std::mutex> lock(guard);
    // ownership: allowed - read under the guard its holder clears it with
    Target* const target = held.load();
    if (target == nullptr) {
        return true;
    }
    return QMetaObject::invokeMethod(
        target, [target, action = std::move(action)]() { action(*target); }, Qt::QueuedConnection);
}

}  // namespace bazarish::app
