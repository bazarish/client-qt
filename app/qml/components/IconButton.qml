import QtQuick
import QtQuick.Controls
import Bazarish

Button {
    id: control
    flat: true
    hoverEnabled: true
    property color tint: Theme.text
    font.pixelSize: 18
    implicitWidth: 40
    implicitHeight: 40
    contentItem: Text {
        text: control.text
        color: control.tint
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 8
        color: control.down ? Theme.surfaceAlt : (control.hovered ? Theme.surface : "transparent")
    }
}
