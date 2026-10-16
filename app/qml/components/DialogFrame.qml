// Bazarish project (c) 2026
import QtQuick
import Bazarish

// The frame every dialog and sheet is drawn in: a thin edge, and a black glow beneath so the
// window lies on a dark patch instead of melting into the screen behind it. An
// account-ending dialog carries the neon edge, so it cannot be mistaken for an ordinary question.
Rectangle {
    id: frame
    property bool destructive: false

    readonly property int kGlowLayers: 10
    readonly property int kGlowStep: 3
    readonly property real kGlowOpacity: 0.14

    color: Theme.bg
    radius: Theme.radius
    border.color: frame.destructive ? Theme.neon : Theme.green
    border.width: 1

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
