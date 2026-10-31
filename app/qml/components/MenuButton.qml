// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Button {
    id: control
    focusPolicy: Theme.pressFocus
    hoverEnabled: true
    property bool danger: false
    property bool positive: false
    property bool primary: false
    readonly property bool filled: primary && enabled
    readonly property real kDisabledOpacity: 0.4
    opacity: enabled ? 1 : kDisabledOpacity
    property string iconName: ""
    property real iconSize: Theme.iconInline
    leftPadding: 14
    rightPadding: 14
    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.filled
            ? (control.down ? Theme.accentDown : (control.hovered ? Theme.accentHover : Theme.accent))
            : control.positive ? Theme.bubbleOut
            : (control.down ? Theme.pressed : (control.hovered ? Theme.surfaceAlt : Theme.surface))
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
