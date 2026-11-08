// Bazarish project (c) 2026
#pragma once

#include <functional>

namespace bazarish::app {

// Blocking work off the thread that asked, on a thread of its own that is held until it ends.
void inBackground(std::function<void()> work);

}  // namespace bazarish::app
