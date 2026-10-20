// Bazarish project (c) 2026
import QtQuick
import Bazarish

// A field's press-and-hold, listened to only on a phone. A listened press-and-hold
// keeps Qt from handing the field the release that would drop the selection made.
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
