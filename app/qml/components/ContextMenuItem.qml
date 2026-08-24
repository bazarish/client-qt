// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// One entry of a ContextMenu. A menu styles only the entries it creates itself,
// so entries written by hand need their own look or they come out in the stock
// white; this is that look, in one place. An entry may carry an icon, drawn
// before its label, so a menu can be read at a glance and in any language.
MenuItem {
    id: root
    // Set on an entry that removes something, so it reads as one.
    property bool danger: false
    // Name from the shared icon set, or empty for a menu of plain words.
    property string iconName: ""
    readonly property int kIconSize: 15
    readonly property int kIconGap: 8

    implicitWidth: label.implicitWidth + 28
        + (root.iconName.length > 0 ? root.kIconSize + root.kIconGap : 0)
    implicitHeight: label.implicitHeight + 14

    contentItem: Item {
        implicitHeight: label.implicitHeight
        Icon {
            id: glyph
            visible: root.iconName.length > 0
            name: root.iconName
            size: root.kIconSize
            color: label.color
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            id: label
            text: root.text
            color: !root.enabled ? Theme.textDim : (root.danger ? Theme.danger : Theme.text)
            verticalAlignment: Text.AlignVCenter
            anchors.left: root.iconName.length > 0 ? glyph.right : parent.left
            anchors.leftMargin: root.iconName.length > 0 ? root.kIconGap : 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    background: Rectangle {
        color: root.highlighted ? Theme.surfaceAlt : "transparent"
        radius: Theme.radiusSmall
    }
}
