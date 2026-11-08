// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls as Controls
import Bazarish

Controls.TextField {
    id: field
    background: Rectangle {
        radius: 8
        color: Theme.control
        border.color: field.activeFocus ? Theme.accent : Theme.controlEdge
    }
    PhoneSelection { field: field }
}
