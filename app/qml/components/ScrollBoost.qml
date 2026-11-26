// Bazarish project (c) 2026
import QtQuick
import Bazarish

Item {
    id: boost
    required property Flickable flickable

    readonly property int kStreakMs: 700
    readonly property real kStreakGain: 0.5
    readonly property real kMaxGain: 4
    // Qt scrolls a notch by the wheel's lines of 24 px.
    readonly property real kLinePixels: 24
    readonly property real kNotchPixels: Application.styleHints.wheelScrollLines * kLinePixels
    readonly property real kNotchAngle: 120
    readonly property real kGlideMs: 40
    readonly property int kGlideTickMs: 16
    readonly property real kSettledPixels: 0.5
    readonly property real baseVelocity: flickable.maximumFlickVelocity / kMaxGain

    property int streak: 0
    property real lastAt: 0
    property int lastWay: 0
    property bool steering: false
    property real gliding: 0
    readonly property bool wheeling: gliding !== 0

    function gainFor(way) {
        const now = Date.now()
        streak = way === lastWay && now - lastAt < kStreakMs ? streak + 1 : 1
        lastAt = now
        lastWay = way
        return Math.min(1 + (streak - 1) * kStreakGain, kMaxGain)
    }

    // A flick's velocity raises contentY when negative, so a way along contentY flicks with the opposite sign.
    function flickAlong(velocity) {
        steering = true
        flickable.flick(0, -velocity)
        steering = false
    }

    // Moved by what is left rather than to a point: a page loaded above shifts contentY under the glide.
    function glideBy(pixels) {
        const top = flickable.originY - flickable.topMargin
        const bottom = Math.max(top, flickable.originY + flickable.contentHeight
            + flickable.bottomMargin - flickable.height)
        const wanted = flickable.contentY + pixels
        const to = Math.max(top, Math.min(wanted, bottom))
        flickable.contentY = to
        if (to !== wanted) {
            gliding = 0
        }
    }

    Component.onCompleted: if (App.mobile) { flickable.maximumFlickVelocity *= kMaxGain }

    Connections {
        target: App.mobile ? boost.flickable : null
        function onFlickStarted() {
            if (boost.steering) {
                return
            }
            const velocity = boost.flickable.verticalVelocity
            const way = velocity > 0 ? 1 : -1
            const capped = Math.min(Math.abs(velocity), boost.baseVelocity)
            boost.flickAlong(way * capped * boost.gainFor(way))
        }
    }

    Timer {
        id: glide
        interval: boost.kGlideTickMs
        repeat: true
        running: boost.gliding !== 0
        property real lastTick: 0
        onRunningChanged: glide.lastTick = Date.now()
        onTriggered: {
            const now = Date.now()
            const share = 1 - Math.exp(-(now - glide.lastTick) / boost.kGlideMs)
            glide.lastTick = now
            const step = Math.abs(boost.gliding) <= boost.kSettledPixels
                ? boost.gliding : boost.gliding * share
            const left = boost.gliding - step
            boost.glideBy(step)
            if (boost.wheeling) {
                boost.gliding = left
            }
        }
    }

    WheelHandler {
        parent: boost.flickable
        enabled: !App.mobile
        acceptedDevices: PointerDevice.Mouse
        onWheel: function(event) {
            if (event.angleDelta.y === 0) {
                event.accepted = false
                return
            }
            boost.flickable.cancelFlick()
            boost.gliding -= boost.kNotchPixels * event.angleDelta.y / boost.kNotchAngle
            event.accepted = true
        }
    }
}
