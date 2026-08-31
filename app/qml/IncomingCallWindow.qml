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
Window {
    id: root
    // True while the user is looking at the main window, which shows the same
    // call with the same two answers - there is no reason to cover it.
    property bool mainWindowActive: false

    readonly property bool ringing: App.ringingPeer.length > 0
    visible: root.ringing && !root.mainWindowActive
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Theme.bg
    width: 320
    height: 300
    x: Screen.virtualX + Math.round((Screen.width - width) / 2)
    y: Screen.virtualY + Math.round((Screen.height - height) / 2)
    title: "Incoming call"

    component CallAnswer: Button {
        id: control
        // The fill this button carries, and the ink that reads on it.
        property color fill: Theme.surface
        property color label: Theme.text
        hoverEnabled: true
        implicitHeight: 40
        leftPadding: 18
        rightPadding: 18
        background: Rectangle {
            radius: Theme.radiusSmall
            color: control.down ? Qt.darker(control.fill, 1.25)
                : (control.hovered ? Qt.darker(control.fill, 1.1) : control.fill)
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: Label {
            text: control.text
            color: control.label
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        border.color: Theme.neon
        border.width: 2

        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 40
            spacing: 10

            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 96
                implicitHeight: 96
                // One ring, going out and fading, for as long as it rings. The
                // window is otherwise still: this is the only thing on screen
                // saying the call is live rather than a picture of one.
                Rectangle {
                    id: pulse
                    anchors.centerIn: parent
                    width: 72
                    height: 72
                    radius: width / 2
                    color: "transparent"
                    border.color: Theme.neon
                    border.width: 2
                    opacity: 0
                    SequentialAnimation {
                        running: root.visible
                        loops: Animation.Infinite
                        ParallelAnimation {
                            NumberAnimation {
                                target: pulse; property: "width"; from: 72; to: 96; duration: 1200
                            }
                            NumberAnimation {
                                target: pulse; property: "height"; from: 72; to: 96; duration: 1200
                            }
                            NumberAnimation {
                                target: pulse; property: "opacity"; from: 0.55; to: 0; duration: 1200
                            }
                        }
                    }
                }
                Avatar {
                    anchors.centerIn: parent
                    fingerprint: App.ringingPeerFingerprint
                    size: 72
                }
            }

            Label {
                Layout.fillWidth: true
                text: App.ringingPeer
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: "Incoming audio call"
                color: Theme.textDim
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                // Which account is being called, when more than one is open.
                visible: App.ringingAccountName.length > 0
                Layout.fillWidth: true
                text: "to " + App.ringingAccountName
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.topMargin: 8
                Layout.fillWidth: true
                spacing: 12
                CallAnswer {
                    Layout.fillWidth: true
                    text: "Decline"
                    fill: Theme.danger
                    label: Theme.bg
                    onClicked: App.declineRinging()
                }
                CallAnswer {
                    Layout.fillWidth: true
                    text: "Accept"
                    fill: Theme.accent
                    label: Theme.accentInk
                    onClicked: App.answerRinging()
                }
            }
        }
    }
}
