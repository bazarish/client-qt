// Bazarish project (c) 2026
import QtQuick
import Bazarish

// The frame a dialog or a sheet is drawn in. The one thing that varies is the
// edge: an account-ending dialog carries the neon one, so it cannot be mistaken
// for an ordinary question.
Rectangle {
    property bool destructive: false

    color: Theme.bg
    radius: Theme.radius
    border.color: destructive ? Theme.neon : Theme.border
    // A Rectangle draws a one-pixel border by default; the neon edge is doubled
    // so it reads as a warning rather than as a frame.
    border.width: destructive ? 2 : 1
}
