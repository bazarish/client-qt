// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish
import "Format.js" as Format

// Recording a voice message. What the microphone is picking up is drawn while it
// records, so a microphone that is not working shows itself as a flat line
// rather than as a message that turns out to be silence. The take is heard
// before it goes, and confirming sends it as it stands - a voice message carries
// no text, so there is nothing else to fill in.
Popup {
    id: root
    property var session: null

    modal: true
    focus: true
    readonly property int kLeastWidth: 420
    readonly property int kEdge: 14
    width: Math.min(parent ? parent.width - 24 : root.kLeastWidth,
        Math.max(root.kLeastWidth, actions.implicitWidth + 2 * root.kEdge))
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogFrame { }

    readonly property bool recording: root.session ? root.session.voiceRecording : false
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
            // Opening the window is asking to record, as in other messengers.
            voiceMicrophone.whenGranted(() => root.session.startVoiceRecording())
        }
    }
    MicrophoneGate {
        id: voiceMicrophone
        onRefused: {
            root.close()
            window.showToast(qsTr("No access to the microphone."))
        }
    }

    // Leaving throws the take away: a recording nobody confirmed is not a draft.
    onClosed: if (root.session) {
        root.session.cancelVoiceRecording()
    }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onVoiceChanged() {
            if (!root.visible || !root.session.voiceRecording) {
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
                text: qsTr("Voice message")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

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
                            if (root.recording) {
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
                        opacity: root.recording || root.takeReady ? 1.0 : 0.35

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
                    ? Format.minutesSeconds(Math.floor(root.session.voiceElapsedMs / 1000))
                    : (root.takeReady ? Format.minutesSeconds(Math.floor(root.session.voiceTakeMs / 1000)) : "0:00")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                Layout.fillWidth: true
            }
            Label {
                visible: root.takeReady
                text: Format.bytes(root.session ? root.session.voiceTakeBytes : 0)
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
            id: actions
            readonly property int kButton: 56
            readonly property int kIcon: 24
            Layout.alignment: Qt.AlignHCenter
            Layout.margins: root.kEdge
            spacing: 24

            // Where to press first: the light under it breathes until a take exists.
            Item {
                id: recordSpot
                readonly property int kDiameter: actions.kButton
                readonly property int kDot: 18
                readonly property int kStopCorner: 2
                readonly property int kBreathMs: 900
                readonly property real kBreathLevel: 1
                readonly property int kGlowReach: 12
                Layout.preferredWidth: recordSpot.kDiameter
                Layout.preferredHeight: recordSpot.kDiameter
                CallGlow {
                    id: recordGlow
                    anchors.centerIn: parent
                    avatarSize: recordSpot.kDiameter + recordSpot.kGlowReach
                    SequentialAnimation on level {
                        running: root.opened && !root.recording && !root.takeReady
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) { recordGlow.level = 0 }
                        NumberAnimation {
                            to: recordSpot.kBreathLevel
                            duration: recordSpot.kBreathMs
                            easing.type: Easing.InOutSine
                        }
                        NumberAnimation {
                            to: 0
                            duration: recordSpot.kBreathMs
                            easing.type: Easing.InOutSine
                        }
                    }
                }
                Button {
                    id: recordButton
                    focusPolicy: Theme.pressFocus
                    anchors.fill: parent
                    padding: 0
                    hoverEnabled: true
                    Accessible.name: root.recording ? qsTr("Stop")
                        : (root.takeReady ? qsTr("Record again") : qsTr("Record"))
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    background: Rectangle {
                        radius: width / 2
                        color: recordButton.down ? Theme.border2
                            : (recordButton.hovered ? Theme.surfaceAlt : Theme.surface)
                        border.color: recordButton.hovered ? Theme.danger : Theme.border
                    }
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: recordSpot.kDot
                            height: width
                            radius: root.recording ? recordSpot.kStopCorner : width / 2
                            color: Theme.danger
                        }
                    }
                    onClicked: {
                        if (root.recording) {
                            root.session.stopVoiceRecording()
                        } else {
                            root.clearLevels()
                            voiceMicrophone.whenGranted(() => root.session.startVoiceRecording())
                        }
                    }
                }
            }
            MenuButton {
                iconName: root.takePlaying ? "stop" : "play"
                Accessible.name: root.takePlaying ? qsTr("Stop") : qsTr("Listen")
                enabled: root.takeReady
                iconSize: actions.kIcon
                leftPadding: 0
                rightPadding: 0
                Layout.preferredWidth: actions.kButton
                Layout.preferredHeight: actions.kButton
                onClicked: root.session.playVoiceTake()
            }
            MenuButton {
                iconName: "send"
                Accessible.name: qsTr("Send")
                iconSize: actions.kIcon
                leftPadding: 0
                rightPadding: 0
                Layout.preferredWidth: actions.kButton
                Layout.preferredHeight: actions.kButton
                enabled: root.takeReady || root.recording
                primary: true
                onClicked: {
                    if (root.recording) {
                        root.session.stopVoiceRecording()
                    }
                    // A take too short to send stays here with its error.
                    if (root.session.voiceTakeReady) {
                        root.session.sendVoiceTake()
                        root.close()
                    }
                }
            }
        }
    }
}
