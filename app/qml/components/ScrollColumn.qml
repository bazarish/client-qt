// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    default property alias content: column.data
    property real columnWidth: availableWidth
    property bool centered: false
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    contentHeight: column.height
    clip: true

    readonly property Item focusedItem: Window.activeFocusItem
    onFocusedItemChanged: revealFocused()
    onAvailableHeightChanged: revealFocused()

    function revealFocused() {
        let inside = false
        for (let up = root.focusedItem; up; up = up.parent) {
            if (up === column) {
                inside = true
                break
            }
        }
        if (!inside) {
            return
        }
        const flick = root.contentItem
        const top = root.focusedItem.mapToItem(column, 0, 0).y
        const bottom = top + root.focusedItem.height
        if (top < flick.contentY) {
            flick.contentY = top
        } else if (bottom > flick.contentY + flick.height) {
            flick.contentY = bottom - flick.height
        }
    }

    ColumnLayout {
        id: column
        spacing: root.spacing
        width: root.columnWidth
        x: (root.availableWidth - width) / 2
        y: root.centered ? Math.max(0, (root.availableHeight - height) / 2) : 0
    }
}
