// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Recording a voice message. What the microphone is picking up is drawn while it
// records, so a microphone that is not working shows itself as a flat line
// rather than as a message that turns out to be silence. The take is heard
// before it goes, and confirming sends it as it stands - a voice message carries
// no text, so there is nothing else to fill in.
Popup {
    id: root
    property var session: null

    modal: true
    width: 420
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    readonly property bool recording: root.session ? root.session.voiceRecording : false
    // The microphone is open and drawing, with nothing kept: what the window does
    // from the moment it appears, so a microphone that is not working is visible
    // before a take is spoken into it rather than after.
    readonly property bool watching: root.session ? root.session.voiceMonitoring : false
    readonly property bool takeReady: root.session ? root.session.voiceTakeReady : false
    readonly property bool takePlaying: root.session ? root.session.voiceTakePlaying : false

    // The rolling picture of the microphone, one entry a tick, oldest first.
    readonly property int kBars: 48
    property var levels: []

    // Live loudness is read as decibels, not as a raw ratio: speech and a quiet
    // room are an order of magnitude apart in amplitude and a hair apart on a
    // linear bar. Below the floor there is nothing to draw - which is exactly
    // what a dead microphone gives.
    readonly property real kFloorDb: -50
    function levelHeight(rms) {
        if (rms <= 0) {
            return 0
        }
        const db = 20 * Math.log10(rms)
        return Math.max(0, Math.min(1, (db - kFloorDb) / -kFloorDb))
    }

    function formatDuration(ms) {
        const total = Math.floor(ms / 1000)
        const seconds = total % 60
        return Math.floor(total / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function humanSize(bytes) {
        if (bytes < 1024) {
            return bytes + " B"
        }
        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KB"
        }
        return (bytes / (1024 * 1024)).toFixed(1) + " MB"
    }

    function clearLevels() {
        const empty = []
        for (let i = 0; i < root.kBars; ++i) {
            empty.push(0)
        }
        root.levels = empty
    }

    onOpened: {
        root.clearLevels()
        if (root.session) {
            root.session.discardVoiceTake()
            // The microphone opens with the window, so the line moves before the
            // user commits to a take.
            root.session.startVoiceMonitor()
        }
    }
    // Leaving throws the take away: a recording nobody confirmed is not a draft.
    // The microphone closes with it - nothing is watched once nobody is looking.
    onClosed: if (root.session) {
        root.session.cancelVoiceRecording()
        root.session.stopVoiceMonitor()
    }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onVoiceChanged() {
            // Also while the window is only watching: the point of opening the
            // microphone before a take is that a microphone which is not working
            // shows itself as a flat line before the user speaks into it.
            if (!root.visible || (!root.session.voiceRecording && !root.session.voiceMonitoring)) {
                return
            }
            const next = root.levels.slice(1)
            next.push(root.session.voiceLevel)
            root.levels = next
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label {
                text: "Voice message"
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // The one picture this window is for: live while recording, the take's
        // own shape once there is one, and a flat line when there is neither.
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.preferredHeight: 96
            radius: Theme.radiusSmall
            color: Theme.deep
            border.color: Theme.border

            Row {
                anchors.centerIn: parent
                width: parent.width - 24
                height: parent.height - 24
                spacing: 2
                Repeater {
                    model: root.kBars
                    delegate: Rectangle {
                        required property int index
                        readonly property real level: {
                            if (root.recording || root.watching) {
                                return root.levelHeight(root.levels[index] || 0)
                            }
                            if (root.takeReady) {
                                return waveBar(root.session.voiceTakeWave, index, root.kBars)
                            }
                            return 0
                        }
                        width: (parent.width - (root.kBars - 1) * parent.spacing) / root.kBars
                        height: Math.max(2, level * parent.height)
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 1
                        color: root.recording ? Theme.danger : Theme.accent
                        opacity: root.recording || root.takeReady || root.watching ? 1.0 : 0.35

                        // Reads one bar out of the stored hex account, stretched
                        // over however many bars are drawn here.
                        function waveBar(hex, at, bars) {
                            if (!hex || hex.length === 0) {
                                return 0
                            }
                            const from = Math.floor(at * hex.length / bars)
                            return parseInt(hex.charAt(from), 16) / 15
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Label {
                text: root.recording
                    ? root.formatDuration(root.session.voiceElapsedMs)
                    : (root.takeReady ? root.formatDuration(root.session.voiceTakeMs) : "0:00")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                Layout.fillWidth: true
            }
            Label {
                visible: root.takeReady
                text: root.humanSize(root.session ? root.session.voiceTakeBytes : 0)
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }

        Label {
            visible: text.length > 0
            text: root.session ? root.session.voiceError : ""
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 6
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8

            MenuButton {
                iconName: root.recording ? "stop" : "mic"
                text: root.recording ? "Stop" : (root.takeReady ? "Record again" : "Record")
                onClicked: {
                    if (root.recording) {
                        root.session.stopVoiceRecording()
                    } else {
                        root.clearLevels()
                        root.session.startVoiceRecording()
                    }
                }
            }
            MenuButton {
                iconName: root.takePlaying ? "stop" : "play"
                text: root.takePlaying ? "Stop" : "Listen"
                enabled: root.takeReady
                onClicked: root.session.playVoiceTake()
            }
            Item { Layout.fillWidth: true }
            MenuButton {
                iconName: "send"
                text: "Send"
                enabled: root.takeReady
                onClicked: {
                    root.session.sendVoiceTake()
                    root.close()
                }
            }
        }
    }
}
