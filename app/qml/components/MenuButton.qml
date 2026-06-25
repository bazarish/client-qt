import QtQuick
import QtQuick.Controls
import Bazarish

// A menu / list action button in the shared "terminal" style: a solid surface
// that lifts on hover (surfaceAlt fill + neon outline) and darkens on press, so
// choices are contrasty and clearly react to the cursor. Set `danger: true` for
// a destructive action (it keeps a red outline and red text instead).
Button {
    id: control
    hoverEnabled: true
    property bool danger: false
    leftPadding: 14
    rightPadding: 14
    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down ? Theme.border2 : (control.hovered ? Theme.surfaceAlt : Theme.surface)
        border.width: 1
        border.color: control.hovered ? (control.danger ? Theme.danger : Theme.green)
                                       : (control.danger ? Theme.danger : Theme.border)
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
    contentItem: Label {
        text: control.text
        color: control.enabled ? (control.danger ? Theme.danger : Theme.text) : Theme.textDim
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
