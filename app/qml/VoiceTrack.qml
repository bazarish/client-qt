// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish
import "Format.js" as Format

RowLayout {
    id: root
    property var session: null
    property string e2eId: ""
    property real durationMs: 0
    property string wave: ""
    property bool stretched: false
    readonly property bool playing: !!root.session && root.e2eId.length > 0
        && root.session.voicePlaying === root.e2eId
    spacing: 8

    Row {
        id: bars
        readonly property int kBars: 32
        readonly property int kBarWidth: 2
        readonly property int kMaxHeight: 26
        readonly property int kTopLevel: 15
        readonly property int count: root.stretched
            ? Math.max(kBars, Math.floor((bars.width + spacing) / (kBarWidth + spacing)))
            : kBars
        readonly property int playedBars: {
            if (!root.playing || !(root.durationMs > 0)) {
                return 0
            }
            const fraction = root.session.voicePositionMs / root.durationMs
            return Math.round(Math.min(1, Math.max(0, fraction)) * count)
        }
        function sample(at) {
            return parseInt(root.wave.charAt(at), 16) / kTopLevel
        }
        // Stretched over more bars than the note has points, the levels between them are drawn in a line.
        function levelAt(index) {
            if (!root.stretched) {
                return bars.sample(Math.floor(index * root.wave.length / count))
            }
            const at = index * (root.wave.length - 1) / Math.max(1, count - 1)
            const low = Math.floor(at)
            const high = Math.min(low + 1, root.wave.length - 1)
            return bars.sample(low) + (bars.sample(high) - bars.sample(low)) * (at - low)
        }
        visible: root.wave.length > 0
        Layout.fillWidth: root.stretched
        Layout.preferredWidth: root.stretched ? 0 : kBars * (kBarWidth + spacing) - spacing
        Layout.preferredHeight: kMaxHeight
        spacing: 1
        Repeater {
            model: bars.visible ? bars.count : 0
            delegate: Rectangle {
                required property int index
                readonly property real level: bars.levelAt(index)
                width: bars.kBarWidth
                height: Math.max(2, level * bars.kMaxHeight)
                anchors.verticalCenter: parent.verticalCenter
                radius: 1
                color: index < bars.playedBars ? Theme.green : Theme.textDim
            }
        }
        function seekTo(x) {
            if (!(root.durationMs > 0)) {
                return
            }
            const drawn = bars.count * (bars.kBarWidth + bars.spacing) - bars.spacing
            const fraction = Math.min(1, Math.max(0, x / drawn))
            root.session.playVoice(root.e2eId, Math.round(fraction * root.durationMs))
        }
        TapHandler {
            onTapped: function(point) { bars.seekTo(point.position.x) }
        }
        DragHandler {
            property real lastX: 0
            target: null
            yAxis.enabled: false
            onCentroidChanged: {
                if (active) {
                    lastX = centroid.position.x
                }
            }
            onActiveChanged: {
                if (!active) {
                    bars.seekTo(lastX)
                }
            }
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    Label {
        text: {
            const speed = root.session ? root.session.voiceSpeed : 1
            const leftMs = root.playing
                ? Math.max(0, (root.durationMs - root.session.voicePositionMs) / speed)
                : root.durationMs
            return Format.minutesSeconds(Math.round(leftMs / 1000))
        }
        color: Theme.text
        font.pixelSize: Theme.fontSmall
    }
    Item { visible: !root.stretched; Layout.fillWidth: true }
    Label {
        text: qsTr("%1x").arg(root.session ? root.session.voiceSpeed : 1)
        color: (root.session && root.session.voiceSpeed > 1) ? Theme.accent : Theme.textDim
        font.pixelSize: Theme.fontSmall
        Layout.preferredWidth: Math.ceil(speedWidth.advanceWidth)
        horizontalAlignment: Text.AlignRight
        TextMetrics {
            id: speedWidth
            font.pixelSize: Theme.fontSmall
            text: qsTr("1.5x")
        }
        TapHandler { onTapped: root.session.cycleVoiceSpeed() }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
}
