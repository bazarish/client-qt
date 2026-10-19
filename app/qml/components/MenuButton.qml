import QtQuick
import QtQuick.Controls
import Bazarish

// A menu / list action button in the shared "terminal" style: a solid surface
// that lifts on hover (surfaceAlt fill + neon outline) and darkens on press, so
// choices are contrasty and clearly react to the cursor. Set `danger: true` for
// a destructive action (it keeps a red outline and red text instead).
Button {
    id: control
    focusPolicy: Theme.pressFocus
    hoverEnabled: true
    property bool danger: false
    // Set positive: true for a brief success/confirmation state - a green outline
    // and green text on a green-tinted fill (e.g. an "Add" button flashing
    // "Requested" after a contact request is sent).
    property bool positive: false
    // The one action the window is for, filled once it can be taken.
    property bool primary: false
    readonly property bool filled: primary && enabled
    readonly property real kDisabledOpacity: 0.4
    opacity: enabled ? 1 : kDisabledOpacity
    // A drawn shape to the left of the label, naming what the button does; empty
    // leaves the label alone in the middle as before.
    property string iconName: ""
    property real iconSize: Theme.iconInline
    leftPadding: 14
    rightPadding: 14
    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.filled
            ? (control.down ? Theme.accentDown : (control.hovered ? Theme.accentHover : Theme.accent))
            : control.positive ? Theme.bubbleOut
            : (control.down ? Theme.border2 : (control.hovered ? Theme.surfaceAlt : Theme.surface))
        border.width: 1
        border.color: control.filled ? Theme.accent
            : control.positive ? Theme.green
            : (control.hovered ? (control.danger ? Theme.danger : Theme.green)
                               : (control.danger ? Theme.danger : Theme.border))
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
    contentItem: IconLabel {
        name: control.iconName
        color: !control.enabled ? Theme.textDim
            : control.filled ? Theme.accentText
            : (control.positive ? Theme.green : (control.danger ? Theme.danger : Theme.text))
        available: control.availableWidth
        iconSize: control.iconSize
    }
}
