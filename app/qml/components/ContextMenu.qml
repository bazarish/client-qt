// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Menu {
    margins: Theme.spacing
    // On a phone the menu leaves the focus where it was, so an open keyboard stays open.
    focus: !App.mobile
    // A tap outside only closes the menu; passed through, it opened the menu or a picker again.
    modal: true
    dim: false

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
