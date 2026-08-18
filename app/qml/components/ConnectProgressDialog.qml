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

    // Hiding does not cancel: the connect keeps running and is watchable in the
    // background-activity panel, so the dialog must not pop back up on the next
    // milestone. The suppression lasts until this attempt ends.
    property bool suppressed: false

    modal: true
    closePolicy: Popup.CloseOnEscape
    onClosed: if (root.session && root.session.connecting) { root.suppressed = true }
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 48 : 420, 460)
    padding: 0

    // Open while connecting; close as soon as the attempt ends either way.
    Connections {
        target: root.session
        function onConnectStateChanged() {
            if (root.session.connecting) {
                if (!root.suppressed) {
                    root.open()
                }
            } else {
                root.suppressed = false
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

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Label {
                text: "Hiding keeps it running — watch it in the activity panel on the right."
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                text: "Hide"
                onClicked: root.close()
                background: Rectangle { radius: 8; color: Theme.surfaceAlt; border.color: Theme.border }
                contentItem: Label {
                    text: parent.text; color: Theme.text
                    leftPadding: 12; rightPadding: 12
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Item { Layout.fillWidth: true; implicitHeight: 8 }
    }
}
