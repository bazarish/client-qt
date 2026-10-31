// Bazarish project (c) 2026
#pragma once

class QQuickItem;
class QQuickWindow;

namespace bazarish::app {

bool closeTopPopup(QQuickWindow* window);

// A reopened popup refocuses the field it last had, and a phone raises its keyboard.
void forgetFocusOnClose(QQuickItem* overlay);

}  // namespace bazarish::app
