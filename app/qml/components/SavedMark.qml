// Bazarish project (c) 2026
import QtQuick
import Bazarish

// The saved chat is nobody's face: it carries a bookmark wherever a contact shows an avatar.
Rectangle {
    id: root
    property int size: Theme.avatar
    property int iconSize: size / 2
    implicitWidth: size
    implicitHeight: size
    radius: width / 2
    color: Theme.surfaceAlt
    border.color: Theme.border2
    border.width: Theme.light ? 1 : 0
    Icon {
        anchors.centerIn: parent
        name: "bookmark"
        color: Theme.green
        size: root.iconSize
    }
}
