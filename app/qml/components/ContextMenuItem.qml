// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// A Menu styles only the entries it creates; hand-written ones need their own look.
MenuItem {
    id: root
    focusPolicy: Theme.pressFocus
    property bool danger: false
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
