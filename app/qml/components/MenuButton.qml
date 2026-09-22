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
    // A drawn shape to the left of the label, naming what the button does; empty
    // leaves the label alone in the middle as before.
    property string iconName: ""
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
    contentItem: Item {
        // From the parts, not from the row: the label's width is bound to what
        // the button offers, so asking the row how wide it wants to be would ask
        // the button a question that depends on the answer.
        implicitWidth: label.implicitWidth + (icon.visible ? icon.width + line.spacing : 0)
        implicitHeight: Math.max(label.implicitHeight, icon.visible ? icon.height : 0)
        Row {
            id: line
            anchors.centerIn: parent
            spacing: 8
            Icon {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName.length > 0
                name: control.iconName
                color: label.color
                size: Theme.fontBody
            }
            Label {
                id: label
                // The label gives way to the icon instead of pushing it out of
                // the button: what is elided is the text, as it was before.
                width: Math.min(implicitWidth, control.availableWidth
                    - (icon.visible ? icon.width + line.spacing : 0))
                text: control.text
                color: !control.enabled ? Theme.textDim
                    : (control.positive ? Theme.green : (control.danger ? Theme.danger : Theme.text))
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
}
