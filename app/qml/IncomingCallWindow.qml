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

    onVisibleChanged: if (root.visible) { glow.requestPaint() }

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
                id: halo
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 168
                implicitHeight: 168
                // How the light stands at the quietest moment of the ringtone and
                // at its loudest: the pulse is the track's own shape, so a beat
                // lands as light and the gaps between beats go dim rather than
                // dark.
                readonly property real quietScale: 0.8
                readonly property real loudScale: 1.15
                readonly property real quietOpacity: 0.16

                // Painted once; what pulses is how big and how bright it is drawn.
                Canvas {
                    id: glow
                    anchors.fill: parent
                    scale: halo.quietScale
                        + (halo.loudScale - halo.quietScale) * App.ringLevel
                    opacity: halo.quietOpacity + (1 - halo.quietOpacity) * App.ringLevel
                    // The level arrives about thirty times a second; this carries
                    // the light between two of them.
                    Behavior on scale { NumberAnimation { duration: 60; easing.type: Easing.OutQuad } }
                    Behavior on opacity { NumberAnimation { duration: 60 } }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const centre = width / 2
                        const light = function(alpha) {
                            return Qt.rgba(Theme.neon.r, Theme.neon.g, Theme.neon.b, alpha)
                        }
                        const gradient = ctx.createRadialGradient(centre, centre, 0,
                            centre, centre, centre)
                        // Flat under the avatar and falling away outside it: what
                        // is seen is the halo around the picture, not a disc
                        // behind it.
                        gradient.addColorStop(0, light(0.55))
                        gradient.addColorStop(0.42, light(0.5))
                        gradient.addColorStop(0.72, light(0.13))
                        gradient.addColorStop(1, light(0))
                        ctx.fillStyle = gradient
                        ctx.fillRect(0, 0, width, height)
                    }
                    Component.onCompleted: glow.requestPaint()
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
