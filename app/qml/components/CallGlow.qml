import QtQuick
import Bazarish

// The light under a ringing call's avatar, breathing with the ringtone: what it
// draws is the loudness of the sound being heard at that moment, not a timer
// running alongside it. Zero level leaves it dim and still.
//
// It takes no room: put it behind an avatar with anchors.centerIn and it spreads
// past it without moving anything.
Item {
    id: root
    // The loudness of the ringtone, 0 to 1.
    property real level: 0
    // The avatar this stands behind. The light is drawn wider than that, so what
    // is seen is a halo around the picture rather than a disc behind it.
    property int avatarSize: 120
    readonly property real kSpread: 2.17
    // How the light stands at the quietest moment of the ringtone and at its
    // loudest: a beat lands as light, and the gaps between beats go dim rather
    // than dark.
    readonly property real kQuietScale: 0.8
    readonly property real kLoudScale: 1.15
    readonly property real kQuietOpacity: 0.16
    // The level arrives about thirty times a second; this carries the light
    // between two of them.
    readonly property int kSmoothingMs: 60

    implicitWidth: avatarSize
    implicitHeight: avatarSize

    // Painted once; what pulses is how big and how bright it is drawn.
    Canvas {
        id: glow
        anchors.centerIn: parent
        width: Math.round(root.avatarSize * root.kSpread)
        height: width
        scale: root.kQuietScale + (root.kLoudScale - root.kQuietScale) * root.level
        opacity: root.kQuietOpacity + (1 - root.kQuietOpacity) * root.level
        Behavior on scale {
            NumberAnimation { duration: root.kSmoothingMs; easing.type: Easing.OutQuad }
        }
        Behavior on opacity { NumberAnimation { duration: root.kSmoothingMs } }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const centre = width / 2
            const light = function(alpha) {
                return Qt.rgba(Theme.neon.r, Theme.neon.g, Theme.neon.b, alpha)
            }
            const gradient = ctx.createRadialGradient(centre, centre, 0, centre, centre, centre)
            // Flat under the avatar and falling away outside it.
            gradient.addColorStop(0, light(0.55))
            gradient.addColorStop(0.42, light(0.5))
            gradient.addColorStop(0.72, light(0.13))
            gradient.addColorStop(1, light(0))
            ctx.fillStyle = gradient
            ctx.fillRect(0, 0, width, height)
        }
        onWidthChanged: glow.requestPaint()
        Component.onCompleted: glow.requestPaint()
    }

    // A window that was hidden comes back without what was painted in it.
    onVisibleChanged: if (root.visible) { glow.requestPaint() }
}
