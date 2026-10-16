// Bazarish project (c) 2026
#pragma once

class QQuickWindow;

namespace bazarish::app {

// Closes the topmost open popup if its policy allows Escape; true when a popup took the press.
bool closeTopPopup(QQuickWindow* window);

}  // namespace bazarish::app
