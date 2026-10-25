// Bazarish project (c) 2026
import QtQuick

// Gestures and wheel notches that follow one another the same way go faster and faster.
Item {
    id: boost
    required property Flickable flickable

    readonly property int kStreakMs: 700
    readonly property real kStreakGain: 0.5
    readonly property real kMaxGain: 4
    // Qt's own step for one notch, measured; a single notch keeps it.
    readonly property real kNotchPixels: 72
    readonly property real kNotchAngle: 120
    readonly property real baseVelocity: flickable.maximumFlickVelocity / kMaxGain

    property int streak: 0
    property real lastAt: 0
    property int lastWay: 0
    property bool steering: false

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

    Component.onCompleted: flickable.maximumFlickVelocity *= kMaxGain

    Connections {
        target: boost.flickable
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

    WheelHandler {
        parent: boost.flickable
        acceptedDevices: PointerDevice.Mouse
        onWheel: function(event) {
            if (event.angleDelta.y === 0) {
                event.accepted = false
                return
            }
            const way = event.angleDelta.y > 0 ? -1 : 1
            const step = boost.kNotchPixels * Math.abs(event.angleDelta.y) / boost.kNotchAngle
            const moving = boost.flickable.verticalVelocity
            const deceleration = boost.flickable.flickDeceleration
            const left = moving * way > 0 ? moving * moving / (2 * deceleration) : 0
            const distance = left + step * boost.gainFor(way)
            boost.flickAlong(way * Math.sqrt(2 * deceleration * distance))
            event.accepted = true
        }
    }
}
