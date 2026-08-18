// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Shown while a connect runs. A first connect over I2P is minutes of real work -
// fetching a network database, starting the router, building tunnels, finding the
// server - so the user watches it progress instead of a button that looks dead.
// The steps come from the core, not from a timer.
Dialog {
    id: root
    property var session: null

    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 48 : 420, 460)
    padding: 0

    // Open while connecting; close as soon as the attempt ends either way.
    Connections {
        target: root.session
        function onConnectStateChanged() {
            if (root.session.connecting) {
                root.open()
            } else {
                root.close()
            }
        }
    }

    background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 14
        Layout.fillWidth: true

        Label {
            text: "Connecting this account"
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 16
            bottomPadding: 0
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            ProgressBar {
                id: bar
                Layout.fillWidth: true
                from: 0; to: 100
                value: root.session ? root.session.connectPercent : 0
                Behavior on value { NumberAnimation { duration: 250 } }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: root.session ? root.session.connectPhase : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    text: (root.session ? root.session.connectPercent : 0) + "%"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }
            Label {
                text: "The first connection over I2P builds tunnels and can take several minutes. "
                    + "You can keep the app open; it will finish on its own."
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        Item { Layout.fillWidth: true; implicitHeight: 8 }
    }
}
