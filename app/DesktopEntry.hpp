// Bazarish project (c) 2026
#pragma once

namespace bazarish::app {

// Installs the desktop entry this application is known by, next to an icon it can
// be drawn with. A window carries its own icon on X11, but a Wayland desktop has
// no such thing: the shell finds the icon by matching the window to an installed
// entry, and without one the dock draws a blank. Only done when running from an
// AppImage, which installs nothing of itself; a packaged build already has both.
// Rewritten on every start, so the entry follows the image if it moves.
void ensureDesktopEntry();

}  // namespace bazarish::app
