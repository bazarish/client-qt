// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// The one-word state of a profile's link to its server, as a fixed-size chip.
// Every row carries one whatever the state is - a row with nothing to say used to
// leave a hole where its neighbours had text, and the grid drifted with it.
Item {
    id: root
    // One of: configured, not configured, connecting, connected, off.
    property string state: "off"

    readonly property bool positive: state === "connected"
    readonly property bool waiting: state === "connecting"
    readonly property bool absent: state === "not configured" || state === "off"

    // Wide enough for the longest label the chip ever draws ("not configured"),
    // so the text is never cut and a row keeps its shape when the state changes
    // under it.
    implicitWidth: 108
    implicitHeight: label.implicitHeight + 6

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: root.positive ? Theme.green : "transparent"
        border.color: root.positive ? Theme.green : (root.waiting ? Theme.warn : Theme.border)
        border.width: 1
        Label {
            id: label
            anchors.centerIn: parent
            text: root.state
            color: root.positive ? Theme.text : (root.waiting ? Theme.warn : Theme.textDim)
            font.pixelSize: Theme.fontSmall - 1
            font.weight: Font.Medium
        }
    }
}
