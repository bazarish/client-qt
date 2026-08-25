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
    closePolicy: Popup.NoAutoClose  // dismissed only through call actions
    anchors.centerIn: Overlay.overlay
    width: Math.min(360, parent ? parent.width - 24 : 360)
    height: Math.min(440, parent ? parent.height - 24 : 440)
    padding: 18

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

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
        // Speech sits low in the range a level meter can show, so what is drawn is
        // lifted to where the eye reads it.
        readonly property real gain: 4
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
        // A block exactly as tall as the tallest bar can be. The bars rise and
        // fall inside it, so a voice no longer moves everything else in the window
        // up and down with it.
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
                            return at >= 0 ? Math.min(1, meter.history[at] * meter.gain) : 0
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
    component CallButton: Button {
        id: callButton
        property color fill: Theme.accent
        property color label: "white"
        // One width for every call action: a row of buttons that size themselves
        // to their labels is a row that is never centred under the avatar.
        Layout.preferredWidth: 120
        padding: 0
        hoverEnabled: true
        HoverHandler { enabled: callButton.enabled; cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: 24
            // A request in flight dims its button, so a press that is already
            // being carried out does not look like one that was ignored.
            color: !callButton.enabled ? Theme.surfaceAlt
                : callButton.down ? Qt.darker(callButton.fill, 1.2)
                : callButton.hovered ? Qt.lighter(callButton.fill, 1.15)
                : callButton.fill
            border.color: callButton.hovered && callButton.enabled ? Theme.text : Theme.border
            implicitWidth: 120
            implicitHeight: 48
            Behavior on color { ColorAnimation { duration: 90 } }
        }
        contentItem: Label {
            text: callButton.text
            color: callButton.enabled ? callButton.label : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // A round icon action. The microphone toggle is one: a drawn mic says what it
    // is, and crossed out in red says it is off, in the one glance a call allows.
    component CallIconButton: Button {
        id: iconButton
        property color fill: Theme.surface
        property string iconName: ""
        property bool crossed: false
        readonly property int diameter: 48
        readonly property real iconSize: 22
        Layout.preferredWidth: diameter
        padding: 0
        hoverEnabled: true
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: iconButton.diameter / 2
            color: iconButton.down ? Qt.darker(iconButton.fill, 1.2)
                : iconButton.hovered ? Qt.lighter(iconButton.fill, 1.35)
                : iconButton.fill
            border.color: iconButton.hovered ? Theme.text : Theme.border
            implicitWidth: iconButton.diameter
            implicitHeight: iconButton.diameter
            Behavior on color { ColorAnimation { duration: 90 } }
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: iconButton.iconName
                color: Theme.text
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
            onClicked: root.minimizeRequested()
        }

        // --- Audio (or pre-connect) layout: avatar + status + controls. ---
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16
            Item { Layout.fillHeight: true }
            Avatar {
                Layout.alignment: Qt.AlignHCenter
                fingerprint: root.session ? root.session.callPeer : ""
                size: 120
                enlargeable: true
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
                text: root.pending === "accepting" ? "Accepting…"
                    : root.pending === "declining" ? "Declining…"
                    : root.pending === "ending" ? "Ending the call…"
                    : root.callState === "outgoing"
                        ? (root.session && root.session.callStage.length > 0
                            ? root.session.callStage : "Calling…")
                    : root.callState === "incoming" ? "Incoming audio call"
                    : root.callState === "active"
                        ? (!root.connected
                            ? (root.session && root.session.callStage.length > 0
                                ? root.session.callStage : "Opening the audio path")
                            : ((root.session && root.session.callMuted)
                                ? "In call (muted)" : "In call"))
                    : ""
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
                    caption: (root.session && root.session.callMuted) ? "You (muted)" : "You"
                    level: (root.session && !root.session.callMuted)
                        ? root.session.callInputLevel : 0
                    tint: (root.session && root.session.callMuted) ? Theme.warn : Theme.accent
                }
                LevelMeter {
                    caption: root.session ? root.session.callPeerName : "Them"
                    level: root.session ? root.session.callOutputLevel : 0
                    tint: Theme.green
                }
            }
            Item { Layout.fillHeight: true }
            CallControls { Layout.alignment: Qt.AlignHCenter }
        }

    }

    // Shared control row, state-driven; used by both layouts.
    component CallControls: RowLayout {
        spacing: 20

        // Incoming: decline / accept.
        CallButton {
            visible: root.callState === "incoming"
            text: "Decline"; fill: Theme.danger
            enabled: root.pending.length === 0
            onClicked: { root.pending = "declining"; root.session.declineCall() }
        }
        CallButton {
            visible: root.callState === "incoming"
            text: "Accept"; fill: Theme.accent; label: Theme.accentInk
            enabled: root.pending.length === 0
            onClicked: { root.pending = "accepting"; root.session.acceptCall() }
        }

        // Active: mute, end.
        CallIconButton {
            visible: root.callState === "active"
            iconName: "mic"
            crossed: root.session && root.session.callMuted
            Accessible.name: (root.session && root.session.callMuted)
                ? "Turn the microphone on" : "Turn the microphone off"
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: root.session.setCallMuted(!root.session.callMuted)
        }
        CallButton {
            visible: root.callState === "active"
            text: "End"; fill: Theme.danger
            enabled: root.pending.length === 0
            onClicked: { root.pending = "ending"; root.session.endCall() }
        }

        // Outgoing: cancel.
        CallButton {
            visible: root.callState === "outgoing"
            text: "Cancel"; fill: Theme.danger
            enabled: root.pending.length === 0
            onClicked: { root.pending = "ending"; root.session.endCall() }
        }
    }
}
