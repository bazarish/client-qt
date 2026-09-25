import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Bazarish

// An incoming call, put where it cannot be missed: its own window in the middle
// of the screen, above whatever the user is doing, with both answers on it.
//
// Deliberately not a tray popup. A popup fades on its own after a few seconds,
// and a call that faded is a call missed; this stands for exactly as long as the
// call rings and goes when it stops.
//
// Inside, it is the call screen the main window shows: the same avatar, the same
// lines, the same buttons at the same distances. Whichever of the two a call is
// answered from, it is the same thing being answered.
Window {
    id: root
    // True while the user is looking at the main window, which shows the same
    // call with the same two answers - there is no reason to cover it.
    property bool mainWindowActive: false

    readonly property bool ringing: App.ringingPeer.length > 0
    // Which account is being called, and that this is Bazarish asking. With
    // several accounts open one contact can be in more than one of them, and then
    // their name alone does not say who is being called.
    readonly property string heading: App.ringingAccountName + " - Bazarish"

    visible: root.ringing && !root.mainWindowActive
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Theme.bg
    // The size of the call screen inside the main window: this is that screen,
    // taken out of doors.
    width: 360
    height: 440
    x: Screen.virtualX + Math.round((Screen.width - width) / 2)
    y: Screen.virtualY + Math.round((Screen.height - height) / 2)
    title: root.heading

    // The call screen's own action button, kept identical here.

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        // There is no frame of the desktop's to stand in, so the window carries
        // its own edge - and this is not a window to overlook.
        border.color: Theme.neon
        border.width: 2

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16

            // Where this came from, in place of the title bar a frameless window
            // does not have.
            Label {
                Layout.fillWidth: true
                text: root.heading
                color: Theme.green
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Item { Layout.fillHeight: true }

            // The avatar in its own light. The glow is drawn around it and takes
            // no room of its own, so what this lays out is the avatar and nothing
            // else - the same 120 across as in the main window.
            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 120
                implicitHeight: 120
                CallGlow {
                    anchors.centerIn: parent
                    avatarSize: 120
                    level: App.ringLevel
                }
                Avatar {
                    anchors.centerIn: parent
                    fingerprint: App.ringingPeerFingerprint
                    size: 120
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: App.ringingPeer
                color: Theme.text
                font.pixelSize: Theme.fontTitle
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textDim
                text: "Incoming audio call"
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 20
                CallButton {
                    text: "Decline"
                    fill: Theme.danger
                    onClicked: App.declineRinging()
                }
                CallButton {
                    text: "Accept"
                    fill: Theme.accent
                    label: Theme.accentInk
                    onClicked: App.answerRinging()
                }
            }
        }
    }
}
