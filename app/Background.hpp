// Bazarish project (c) 2026
#pragma once

#include <functional>

namespace bazarish::app {

void inBackground(std::function<void()> work);

}  // namespace bazarish::app
