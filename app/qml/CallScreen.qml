import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Audio call overlay. Driven entirely by the controller's callState; opened and
// closed by MainView as the state leaves/returns to "idle".
Popup {
    id: root
    property var session: null
    readonly property string callState: session ? session.callState : "idle"

    modal: true
    closePolicy: Popup.NoAutoClose  // dismissed only through call actions
    anchors.centerIn: Overlay.overlay
    width: 360
    height: 440
    padding: 18

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // A round, coloured action button.
    component CallButton: Button {
        property color fill: Theme.accent
        property color label: "white"
        background: Rectangle {
            radius: 24
            color: parent.down ? Qt.darker(parent.fill, 1.2) : parent.fill
            border.color: Theme.border
            implicitWidth: 120
            implicitHeight: 48
        }
        contentItem: Label {
            text: parent.text
            color: parent.label
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: ColumnLayout {
        spacing: 16
        Item { Layout.fillHeight: true }
        Avatar {
            Layout.alignment: Qt.AlignHCenter
            fingerprint: root.session ? root.session.callPeer : ""
            size: 120
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
            text: root.callState === "outgoing" ? "Calling..."
                : root.callState === "incoming" ? "Incoming audio call"
                : root.callState === "active"
                    ? ((root.session && root.session.callMuted) ? "In call (muted)" : "In call")
                : ""
        }
        Item { Layout.fillHeight: true }

        // Incoming: decline / accept.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 24
            visible: root.callState === "incoming"
            CallButton { text: "Decline"; fill: Theme.danger; onClicked: root.session.declineCall() }
            CallButton { text: "Accept"; fill: Theme.success; onClicked: root.session.acceptCall() }
        }

        // Active: mute toggle / end.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 24
            visible: root.callState === "active"
            CallButton {
                text: (root.session && root.session.callMuted) ? "Unmute" : "Mute"
                fill: Theme.surface
                label: Theme.text
                onClicked: root.session.setCallMuted(!root.session.callMuted)
            }
            CallButton { text: "End"; fill: Theme.danger; onClicked: root.session.endCall() }
        }

        // Outgoing: cancel.
        CallButton {
            Layout.alignment: Qt.AlignHCenter
            visible: root.callState === "outgoing"
            text: "Cancel"
            fill: Theme.danger
            onClicked: root.session.endCall()
        }
    }
}
