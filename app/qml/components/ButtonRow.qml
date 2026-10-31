// Bazarish project (c) 2026
import QtQuick
import QtQuick.Layouts

GridLayout {
    columnSpacing: 8
    rowSpacing: 8
    readonly property real sideBySideWidth: {
        let total = 0
        let shown = 0
        for (let i = 0; i < children.length; ++i) {
            if (children[i].visible) {
                total += children[i].implicitWidth
                ++shown
            }
        }
        return total + Math.max(0, shown - 1) * columnSpacing
    }
    columns: width >= sideBySideWidth ? Math.max(1, children.length) : 1
}
