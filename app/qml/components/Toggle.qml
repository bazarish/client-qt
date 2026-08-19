// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// A switch sized for a desktop settings row. The stock control is drawn for
// touch and swallows the line it sits on, so the track is drawn here instead.
Switch {
    id: root

    readonly property int kTrackWidth: 34
    readonly property int kTrackHeight: 18
    readonly property int kHandleInset: 2
    readonly property int kSlideMs: 120
    readonly property real kDisabledOpacity: 0.5

    padding: 0
    implicitWidth: kTrackWidth
    implicitHeight: kTrackHeight

    indicator: Rectangle {
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        implicitWidth: root.kTrackWidth
        implicitHeight: root.kTrackHeight
        radius: height / 2
        color: root.checked ? Theme.green : Theme.deep
        border.color: root.checked ? Theme.green : Theme.border2
        border.width: 1
        opacity: root.enabled ? 1 : root.kDisabledOpacity

        Rectangle {
            x: root.checked ? parent.width - width - root.kHandleInset : root.kHandleInset
            y: root.kHandleInset
            width: parent.height - 2 * root.kHandleInset
            height: width
            radius: height / 2
            color: root.checked ? Theme.text : Theme.textDim
            Behavior on x { NumberAnimation { duration: root.kSlideMs; easing.type: Easing.OutCubic } }
        }
    }
}
