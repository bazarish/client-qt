// Bazarish project (c) 2026
#pragma once

namespace bazarish::app {

// Registers this binary as the handler for bazarish:// links. On a desktop that
// reads desktop entries the entry itself carries the association.
void ensureUrlScheme();

}  // namespace bazarish::app
