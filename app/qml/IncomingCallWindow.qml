// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Bazarish

Window {
    id: root
    property bool mainWindowActive: false

    readonly property bool ringing: App.ringingPeer.length > 0
    readonly property string heading: App.ringingAccountName + " - Bazarish"

    visible: root.ringing && !root.mainWindowActive && !App.mobile
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Theme.bg
    width: 360
    height: 440
    x: Screen.virtualX + Math.round((Screen.width - width) / 2)
    y: Screen.virtualY + Math.round((Screen.height - height) / 2)
    title: root.heading

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        border.color: Theme.neon
        border.width: 2

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16

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
                text: qsTr("Incoming audio call")
                wrapMode: Text.Wrap
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 20
                CallButton {
                    text: qsTr("Decline")
                    fill: Theme.danger
                    onClicked: App.declineRinging()
                }
                CallButton {
                    text: qsTr("Accept")
                    fill: Theme.accent
                    label: Theme.accentInk
                    onClicked: App.answerRinging()
                }
            }
        }
    }
}
