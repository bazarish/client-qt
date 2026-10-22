// Bazarish project (c) 2026
#pragma once

class QQuickItem;
class QQuickWindow;

namespace bazarish::app {

// Closes the topmost open popup if its policy allows Escape; true when a popup took the press.
bool closeTopPopup(QQuickWindow* window);

// A popup is a focus scope: opened again, it hands the focus to the field it last had, and a
// phone raises its keyboard. Every popup leaving this overlay, which is closing, forgets it.
void forgetFocusOnClose(QQuickItem* overlay);

}  // namespace bazarish::app
