// Bazarish project (c) 2026
#pragma once

class QQuickItem;
class QQuickWindow;

namespace bazarish::app {

bool closeTopPopup(QQuickWindow* window);

// Otherwise a reopened popup refocuses its last field, and a phone raises its keyboard.
void forgetFocusOnClose(QQuickItem* overlay);

}  // namespace bazarish::app
