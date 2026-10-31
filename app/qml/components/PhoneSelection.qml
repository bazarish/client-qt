// Bazarish project (c) 2026
import QtQuick
import Bazarish

// Listening to the press-and-hold keeps Qt from handing the field a release that drops the selection.
Connections {
    property Item field: null
    property bool held: false
    target: App.mobile ? field : null
    function onPressed() {
        held = false
    }
    function onPressAndHold(event) {
        held = true
        TextSelection.longPressed(field, Qt.point(event.x, event.y))
    }
    function onReleased(event) {
        if (!held) {
            TextSelection.tapped(field, Qt.point(event.x, event.y))
        }
    }
}
