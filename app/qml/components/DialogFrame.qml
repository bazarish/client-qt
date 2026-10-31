// Bazarish project (c) 2026
import QtQuick
import Bazarish

Rectangle {
    id: frame
    property bool destructive: false

    readonly property int kGlowLayers: 10
    readonly property int kGlowStep: 3
    readonly property real kGlowOpacityOnDark: 0.14
    readonly property real kGlowOpacityOnLight: 0.04
    readonly property real kGlowOpacity: Theme.light ? kGlowOpacityOnLight : kGlowOpacityOnDark

    color: Theme.bg
    radius: Theme.radius

    // Drawn over the content, so a list scrolled to the rim never covers the edge.
    Rectangle {
        parent: frame.parent
        z: 1
        anchors.fill: parent
        radius: frame.radius
        color: "transparent"
        border.color: frame.destructive ? Theme.neon : Theme.green
        border.width: 1
    }

    Repeater {
        model: frame.kGlowLayers
        Rectangle {
            required property int index
            readonly property int spread: (index + 1) * frame.kGlowStep
            z: -1
            anchors.fill: parent
            anchors.margins: -spread
            radius: frame.radius + spread
            color: "black"
            opacity: frame.kGlowOpacity * (frame.kGlowLayers - index) / frame.kGlowLayers
        }
    }
}
