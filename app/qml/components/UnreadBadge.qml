import QtQuick
import Bazarish

// The count of messages waiting somewhere the user is not looking. Same shape
// wherever it appears, so a number on a back arrow and one on the account
// switcher read as the same thing.
Rectangle {
    id: root
    property int count: 0
    visible: count > 0
    implicitWidth: Math.max(16, label.implicitWidth + 8)
    implicitHeight: 16
    radius: height / 2
    color: Theme.accent
    Text {
        id: label
        anchors.centerIn: parent
        text: root.count > 99 ? "99+" : root.count
        color: Theme.accentText
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }
}
