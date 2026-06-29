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
    // Set positive: true for a brief success/confirmation state - a green outline
    // and green text on a green-tinted fill (e.g. an "Add" button flashing
    // "Requested" after a contact request is sent).
    property bool positive: false
    leftPadding: 14
    rightPadding: 14
    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.positive ? Theme.bubbleOut
            : (control.down ? Theme.border2 : (control.hovered ? Theme.surfaceAlt : Theme.surface))
        border.width: 1
        border.color: control.positive ? Theme.green
            : (control.hovered ? (control.danger ? Theme.danger : Theme.green)
                               : (control.danger ? Theme.danger : Theme.border))
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
    contentItem: Label {
        text: control.text
        color: !control.enabled ? Theme.textDim
            : (control.positive ? Theme.green : (control.danger ? Theme.danger : Theme.text))
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
