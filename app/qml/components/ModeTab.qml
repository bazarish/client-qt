import QtQuick
import QtQuick.Controls
import Bazarish

// One tab of the transport chooser: a rounded pill in a rounded bar, because
// nothing else in this interface has a square corner.
TabButton {
    id: tab
    implicitHeight: 34
    padding: 0

    contentItem: Text {
        text: tab.text
        color: tab.checked ? Theme.text : Theme.textDim
        font.pixelSize: Theme.fontSmall
        font.weight: tab.checked ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.margins: 3
        radius: Theme.radiusSmall
        color: tab.checked ? Theme.surfaceAlt
            : tab.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
        border.width: tab.checked ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
