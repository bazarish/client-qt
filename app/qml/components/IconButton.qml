import QtQuick
import QtQuick.Controls
import Bazarish

// A square button carrying a drawn icon (see Icon.qml). `icon.name` picks the
// shape; `text` still works for the few buttons that carry a word instead, so
// callers can be moved over one at a time.
Button {
    id: control
    flat: true
    hoverEnabled: true
    property color tint: Theme.text
    // Which shape to draw; empty falls back to `text`.
    property string iconName: ""
    property real iconSize: 18
    font.pixelSize: 18
    implicitWidth: 40
    implicitHeight: 40
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            visible: control.iconName.length > 0
            name: control.iconName
            color: control.tint
            size: control.iconSize
        }
        Text {
            anchors.centerIn: parent
            visible: control.iconName.length === 0
            text: control.text
            color: control.tint
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: 8
        color: control.down ? Theme.border2 : (control.hovered ? Theme.surfaceAlt : "transparent")
    }
}
