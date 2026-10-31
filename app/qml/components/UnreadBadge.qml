// Bazarish project (c) 2026
import QtQuick
import Bazarish

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
        textFormat: Text.PlainText
        anchors.centerIn: parent
        text: root.count > 99 ? "99+" : root.count
        color: Theme.accentText
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }
}
