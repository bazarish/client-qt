import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Bazarish

// Call overlay (audio or video). Driven entirely by the controller's callState;
// opened and closed by MainView as the state leaves/returns to "idle". On a video
// call the remote stream fills the surface with the local camera shown picture-in-
// picture; the VideoOutputs' sinks are handed to the controller's presenters, which
// the call engine renders decoded frames into from its worker threads.
Popup {
    id: root
    property var session: null
    readonly property string callState: session ? session.callState : "idle"
    readonly property bool isVideo: session ? session.callVideo : false

    modal: true
    closePolicy: Popup.NoAutoClose  // dismissed only through call actions
    anchors.centerIn: Overlay.overlay
    width: isVideo ? 520 : 360
    height: isVideo ? 600 : 440
    padding: isVideo ? 0 : 18

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // Hands each VideoOutput's sink to the matching presenter so engine frames
    // render. Safe to call repeatedly (idempotent on the same sink).
    function bindSinks() {
        if (!session || !isVideo) {
            return;
        }
        session.remoteVideo.videoSink = remoteOut.videoSink;
        session.localVideo.videoSink = localOut.videoSink;
    }

    Component.onCompleted: bindSinks()
    Connections {
        target: root.session
        function onCallChanged() { root.bindSinks() }
    }

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

    contentItem: Item {

        // --- Audio (or pre-connect) layout: avatar + status + controls. ---
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16
            visible: !root.isVideo
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
            CallControls { }
        }

        // --- Video layout: remote fills, local picture-in-picture, overlaid
        //     status and controls. ---
        Item {
            anchors.fill: parent
            visible: root.isVideo

            Rectangle {
                anchors.fill: parent
                radius: Theme.radius
                color: "black"
                clip: true

                VideoOutput {
                    id: remoteOut
                    anchors.fill: parent
                    fillMode: VideoOutput.PreserveAspectCrop
                }

                // Local self-view, picture-in-picture in the top-right corner.
                Rectangle {
                    width: 132
                    height: 99
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 12
                    color: "#101010"
                    border.color: Theme.border
                    radius: 6
                    clip: true
                    VideoOutput {
                        id: localOut
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectCrop
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: root.session && !root.session.callCameraOn
                        text: "Camera off"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                }

                // Peer name + status, top-left.
                ColumnLayout {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 14
                    spacing: 2
                    Label {
                        text: root.session ? root.session.callPeerName : ""
                        color: "white"
                        font.pixelSize: Theme.fontTitle
                    }
                    Label {
                        color: "#d0d0d0"
                        text: root.callState === "outgoing" ? "Calling..."
                            : root.callState === "incoming" ? "Incoming video call"
                            : root.callState === "active" ? "In call" : ""
                    }
                }

                // Controls pinned to the bottom over the video.
                Item {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: 18
                    width: parent.width
                    height: 56
                    CallControls { anchors.centerIn: parent }
                }
            }
        }
    }

    // Shared control row, state-driven; used by both layouts.
    component CallControls: RowLayout {
        spacing: 20

        // Incoming: decline / accept.
        CallButton {
            visible: root.callState === "incoming"
            text: "Decline"; fill: Theme.danger
            onClicked: root.session.declineCall()
        }
        CallButton {
            visible: root.callState === "incoming"
            text: "Accept"; fill: Theme.success
            onClicked: root.session.acceptCall()
        }

        // Active: mute, camera (video only), end.
        CallButton {
            visible: root.callState === "active"
            text: (root.session && root.session.callMuted) ? "Unmute" : "Mute"
            fill: Theme.surface; label: Theme.text
            onClicked: root.session.setCallMuted(!root.session.callMuted)
        }
        CallButton {
            visible: root.callState === "active" && root.isVideo
            text: (root.session && root.session.callCameraOn) ? "Camera off" : "Camera on"
            fill: Theme.surface; label: Theme.text
            onClicked: root.session.setCameraEnabled(!root.session.callCameraOn)
        }
        CallButton {
            visible: root.callState === "active"
            text: "End"; fill: Theme.danger
            onClicked: root.session.endCall()
        }

        // Outgoing: cancel.
        CallButton {
            visible: root.callState === "outgoing"
            text: "Cancel"; fill: Theme.danger
            onClicked: root.session.endCall()
        }
    }
}
