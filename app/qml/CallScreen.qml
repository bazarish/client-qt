import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Call overlay. Driven entirely by the controller's callState; opened and closed
// by MainView as the state leaves/returns to "idle".
Popup {
    id: root
    property var session: null
    readonly property string callState: session ? session.callState : "idle"
    // The action asked for and not yet reflected in the state ("", "accepting",
    // "declining", "ending", "muting"). Every call button is a request that has
    // to travel to the other side, so it says so instead of looking ignored.
    property string pending: ""
    property bool leaving: false
    onCallStateChanged: root.pending = ""
    // Emitted when the user collapses the call to MainView's compact banner.
    signal minimizeRequested()

    // Media is flowing: accepted is not connected, and until the first packet
    // arrives both sides are still opening the path. This is the same instant on
    // both of them, so neither says "in call" while the other is still waiting.
    readonly property bool connected: session && session.callConnectedAtMs > 0
    // Ticks the running time while the call is up.
    property int elapsedTick: 0
    Timer {
        running: root.connected
        interval: 1000
        repeat: true
        onTriggered: root.elapsedTick++
    }
    // How long the two sides have been talking, m:ss (h:mm:ss past the hour).
    function callDuration() {
        void root.elapsedTick
        if (!root.connected) {
            return ""
        }
        const total = Math.max(0, Math.floor((Date.now() - root.session.callConnectedAtMs) / 1000))
        const seconds = total % 60
        const minutes = Math.floor(total / 60) % 60
        const hours = Math.floor(total / 3600)
        const mm = (hours > 0 && minutes < 10 ? "0" : "") + minutes
        const ss = (seconds < 10 ? "0" : "") + seconds
        return (hours > 0 ? hours + ":" : "") + mm + ":" + ss
    }

    modal: true
    // A call takes the focus from the message field, so a phone's keyboard does not cover it.
    focus: true
    onOpened: Qt.inputMethod.hide()
    onOpenedChanged: App.setCallScreenShown(opened)
    Component.onDestruction: App.setCallScreenShown(false)
    closePolicy: Popup.NoAutoClose  // dismissed only through call actions
    anchors.centerIn: Overlay.overlay
    // On a phone the call is the whole screen, as a phone call is, its frame off the edge.
    width: App.mobile && parent ? parent.width - 2 * Theme.spacing
        : Math.min(360, parent ? parent.width - 24 : 360)
    height: App.mobile && parent ? parent.height - 2 * Theme.spacing
        : Math.min(440, parent ? parent.height - 24 : 440)
    padding: 18

    background: DialogFrame { }

    // A rolling level meter: one bar per sample of the recent past, newest on the
    // right. Drawn from a timer rather than from the value changing, so a silent
    // line keeps scrolling instead of freezing at whatever it last was - which is
    // the difference between "quiet" and "dead".
    component LevelMeter: ColumnLayout {
        id: meter
        property real level: 0
        property color tint: Theme.accent
        property string caption: ""
        // Sized to fit two of these side by side inside the window: a meter wider
        // than that pushed everything centred in this column off to one side,
        // which is how the buttons came to sit right of centre.
        readonly property int bars: 24
        readonly property int barHeight: 22
        readonly property int barWidth: 3
        readonly property int barGap: 2
        readonly property int sampleMs: 100
        property var history: []
        spacing: 2

        Timer {
            running: root.connected
            interval: meter.sampleMs
            repeat: true
            onTriggered: {
                const next = meter.history.slice(-(meter.bars - 1))
                next.push(meter.level)
                meter.history = next
            }
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: meter.caption
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }
        // As tall as the tallest bar, so the bars never move the rest of the window.
        Item {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: meter.bars * meter.barWidth + (meter.bars - 1) * meter.barGap
            implicitHeight: meter.barHeight
            Row {
                anchors.centerIn: parent
                spacing: meter.barGap
                Repeater {
                    model: meter.bars
                    delegate: Rectangle {
                        required property int index
                        // The history fills from the right: the leftmost bars are
                        // empty until enough has been heard to fill them.
                        readonly property real value: {
                            const at = index - (meter.bars - meter.history.length)
                            return at >= 0 ? Math.min(1, meter.history[at] * Theme.speechGain) : 0
                        }
                        width: meter.barWidth
                        height: Math.max(2, value * meter.barHeight)
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 1
                        color: value > 0.02 ? meter.tint : Theme.border2
                    }
                }
            }
        }
    }

    // A round, coloured action button.

    // A round icon action. The microphone toggle is one: a drawn mic says what it
    // is, and crossed out in red says it is off, in the one glance a call allows.
    component CallIconButton: Button {
        id: iconButton
        focusPolicy: Theme.pressFocus
        property color fill: Theme.surface
        property string iconName: ""
        property bool crossed: false
        property bool lit: false
        readonly property int diameter: 48
        readonly property real iconSize: 22
        Layout.preferredWidth: diameter
        padding: 0
        hoverEnabled: true
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: iconButton.diameter / 2
            readonly property color base: iconButton.lit ? Theme.accent : iconButton.fill
            color: iconButton.down ? Qt.darker(base, 1.2)
                : iconButton.hovered ? Qt.lighter(base, 1.35)
                : base
            border.color: iconButton.hovered ? Theme.text : Theme.border
            implicitWidth: iconButton.diameter
            implicitHeight: iconButton.diameter
            Behavior on color { ColorAnimation { duration: 90 } }
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: iconButton.iconName
                color: iconButton.lit ? Theme.accentInk : Theme.text
                size: iconButton.iconSize
            }
            Rectangle {
                visible: iconButton.crossed
                anchors.centerIn: parent
                width: iconButton.iconSize * 1.3
                height: 2
                radius: 1
                rotation: -45
                color: Theme.danger
            }
        }
    }

    contentItem: Item {

        // Collapse the call to MainView's compact banner (outgoing/active only) so
        // the app stays usable while ringing or on a call.
        IconButton {
            iconName: "chevron"
            visible: root.callState === "outgoing" || root.callState === "active"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 6
            z: 10
            // Over a locked phone the rest of the app stays behind the lock until it is opened.
            onClicked: { root.leaving = true; App.requestUnlocked() }
        }
        Connections {
            target: App
            function onUnlocked() {
                if (root.leaving) {
                    root.leaving = false
                    root.minimizeRequested()
                }
            }
        }

        // --- Audio (or pre-connect) layout: avatar + status + controls. ---
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16
            Item { Layout.fillHeight: true }
            // The avatar, and while the call is ringing the light of the ringtone
            // around it - the same light the window on the desktop shows, because
            // it is the same call and the same sound.
            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 120
                implicitHeight: 120
                CallGlow {
                    anchors.centerIn: parent
                    avatarSize: 120
                    level: root.callState === "incoming" ? App.ringLevel : 0
                }
                Avatar {
                    anchors.centerIn: parent
                    fingerprint: root.session ? root.session.callPeer : ""
                    size: 120
                    enlargeable: true
                }
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: root.session ? root.session.callPeerName : ""
                color: Theme.text
                font.pixelSize: Theme.fontTitle
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textDim
                text: root.pending === "accepting" ? qsTr("Accepting\u2026")
                    : root.pending === "declining" ? qsTr("Declining\u2026")
                    : root.pending === "ending" ? qsTr("Ending the call\u2026")
                    : root.callState === "outgoing"
                        ? (root.session && root.session.callStage.length > 0
                            ? root.session.callStage : qsTr("Calling…"))
                    : root.callState === "incoming" ? qsTr("Incoming audio call")
                    : root.callState === "active"
                        ? (!root.connected
                            ? (root.session && root.session.callStage.length > 0
                                ? root.session.callStage : qsTr("Opening the audio path"))
                            : ((root.session && root.session.callMuted)
                                ? qsTr("In call (muted)") : qsTr("In call")))
                    : ""
                wrapMode: Text.Wrap
            }
            // The running time, once there is a call to time.
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: root.connected
                text: root.callDuration()
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.family: Theme.fontFamily
            }
            // What the microphone hears and what is arriving: with neither, a
            // silent call gives the user nothing to tell a dead line from a
            // quiet one.
            RowLayout {
                // Hugging its contents and centred as a whole: the two meters are
                // a fixed size that fits, so nothing has to be stretched.
                Layout.alignment: Qt.AlignHCenter
                visible: root.connected
                spacing: 24
                LevelMeter {
                    caption: (root.session && root.session.callMuted) ? qsTr("You (muted)") : qsTr("You")
                    level: (root.session && !root.session.callMuted)
                        ? root.session.callInputLevel : 0
                    tint: (root.session && root.session.callMuted) ? Theme.warn : Theme.accent
                }
                LevelMeter {
                    caption: root.session ? root.session.callPeerName : qsTr("Contact")
                    level: root.session ? root.session.callOutputLevel : 0
                    tint: Theme.green
                }
            }
            Item { Layout.fillHeight: true }
            CallControls { Layout.alignment: Qt.AlignHCenter }
        }

    }

    component DeviceCaption: RowLayout {
        property alias icon: glyph.name
        property alias text: caption.text
        spacing: 6
        Icon {
            id: glyph
            size: Theme.iconInline
            color: Theme.textDim
        }
        Label {
            id: caption
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }
    }

    // Shared control row, state-driven; used by both layouts.
    component CallControls: RowLayout {
        spacing: 20

        // Incoming: decline / accept.
        CallButton {
            visible: root.callState === "incoming"
            text: qsTr("Decline"); fill: Theme.danger
            enabled: root.pending.length === 0
            onClicked: { root.pending = "declining"; root.session.declineCall() }
        }
        CallButton {
            visible: root.callState === "incoming"
            text: qsTr("Accept"); fill: Theme.accent; label: Theme.accentInk
            enabled: root.pending.length === 0
            onClicked: {
                root.pending = "accepting"
                acceptMicrophone.whenGranted(() => root.session.acceptCall())
            }
            MicrophoneGate {
                id: acceptMicrophone
                onRefused: {
                    root.pending = ""
                    window.showToast(qsTr("No access to the microphone."))
                }
            }
        }

        // Active: devices, mute, end.
        CallIconButton {
            visible: App.mobile && root.callState === "active"
            iconName: "speaker"
            lit: App.speakerOn
            Accessible.name: qsTr("Speaker")
            onClicked: App.speakerOn = !App.speakerOn
        }
        CallIconButton {
            id: gear
            visible: !App.mobile && root.callState === "active"
            iconName: "gear"
            Accessible.name: qsTr("Audio devices")
            onClicked: devicePicker.visible ? devicePicker.close() : devicePicker.open()

            Popup {
                id: devicePicker
                readonly property int kGap: 10
                property var inputs: []
                property var outputs: []
                width: root.availableWidth
                y: -devicePicker.height - devicePicker.kGap
                padding: 14
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                background: DialogFrame { }
                onAboutToShow: {
                    devicePicker.x = Math.round(gear.mapFromItem(root.contentItem, 0, 0).x)
                    devicePicker.inputs = Audio.inputs()
                    devicePicker.outputs = Audio.outputs()
                }

                contentItem: ColumnLayout {
                    spacing: 4
                    DeviceCaption {
                        icon: "mic"
                        text: qsTr("Input device")
                    }
                    Dropdown {
                        Layout.fillWidth: true
                        model: devicePicker.inputs
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: devicePicker.inputs.findIndex(device => device.chosen)
                        onActivated: Audio.chooseInput(currentValue)
                    }
                    DeviceCaption {
                        Layout.topMargin: 10
                        icon: "speaker"
                        text: qsTr("Output device")
                    }
                    Dropdown {
                        Layout.fillWidth: true
                        model: devicePicker.outputs
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: devicePicker.outputs.findIndex(device => device.chosen)
                        onActivated: Audio.chooseOutput(currentValue)
                    }
                }
            }
        }
        CallIconButton {
            visible: root.callState === "active"
            iconName: "mic"
            crossed: root.session && root.session.callMuted
            Accessible.name: (root.session && root.session.callMuted)
                ? qsTr("Turn the microphone on") : qsTr("Turn the microphone off")
            onClicked: root.session.setCallMuted(!root.session.callMuted)
        }
        CallButton {
            visible: root.callState === "active"
            text: qsTr("End"); fill: Theme.danger
            enabled: root.pending.length === 0
            onClicked: { root.pending = "ending"; root.session.endCall() }
        }

        // Outgoing: cancel.
        CallButton {
            visible: root.callState === "outgoing"
            text: qsTr("Cancel"); fill: Theme.danger
            enabled: root.pending.length === 0
            onClicked: { root.pending = "ending"; root.session.endCall() }
        }
    }
}
