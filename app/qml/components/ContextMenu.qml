// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// The app's right-click / long-press menu. The stock one is a white box as wide
// as the style's minimum, which reads as a different application on a dark
// window; this one takes the theme's surface and is only as wide as its widest
// entry.
Menu {
    id: root

    // Wide enough for the entry that needs the most room, and no wider. Hidden
    // entries (the delegates collapse to zero height) do not count.
    implicitWidth: {
        let widest = 0
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i)
            if (entry && entry.visible) {
                widest = Math.max(widest, entry.implicitWidth)
            }
        }
        return Math.max(kMinimumWidth, widest)
    }
    readonly property int kMinimumWidth: 132

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusSmall
        border.color: Theme.border
        border.width: 1
    }

}
