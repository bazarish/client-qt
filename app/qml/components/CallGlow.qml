// Bazarish project (c) 2026
import QtQuick
import Bazarish

Item {
    id: root
    // The loudness of the ringtone, 0 to 1.
    property real level: 0
    property int avatarSize: 120
    readonly property real kSpread: 2.17
    readonly property real kQuietScale: 0.8
    readonly property real kLoudScale: 1.15
    readonly property real kQuietOpacity: 0.16
    readonly property int kSmoothingMs: 60

    implicitWidth: avatarSize
    implicitHeight: avatarSize

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
