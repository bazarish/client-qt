// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// One entry of a ContextMenu. A menu styles only the entries it creates itself,
// so entries written by hand need their own look or they come out in the stock
// white; this is that look, in one place.
MenuItem {
    id: root
    // Set on an entry that removes something, so it reads as one.
    property bool danger: false

    implicitWidth: label.implicitWidth + 28
    implicitHeight: label.implicitHeight + 14

    contentItem: Label {
        id: label
        text: root.text
        color: !root.enabled ? Theme.textDim : (root.danger ? Theme.danger : Theme.text)
        verticalAlignment: Text.AlignVCenter
        leftPadding: 12
        rightPadding: 12
    }
    background: Rectangle {
        color: root.highlighted ? Theme.surfaceAlt : "transparent"
        radius: Theme.radiusSmall
    }
}
