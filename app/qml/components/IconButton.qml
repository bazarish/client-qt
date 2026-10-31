// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Button {
    id: control
    focusPolicy: Theme.pressFocus
    flat: true
    hoverEnabled: true
    property color tint: Theme.text
    property string iconName: ""
    property real iconSize: 18
    implicitWidth: 40
    implicitHeight: 40
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            color: control.tint
            size: control.iconSize
        }
    }
    background: Rectangle {
        radius: 8
        color: control.down ? Theme.pressed : (control.hovered ? Theme.surfaceAlt : "transparent")
    }
}
