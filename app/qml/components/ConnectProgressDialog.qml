// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    property var session: null

    property bool suppressed: false

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    onClosed: if (root.session && root.session.connecting) { root.suppressed = true }
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 2 * Theme.spacing : 420, 460)
    padding: 0

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

    background: DialogFrame { }

    contentItem: ColumnLayout {
        spacing: 14
        Layout.fillWidth: true

        Label {
            text: qsTr("Connecting this account")
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 16
            bottomPadding: 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            ProgressBar {
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
                text: qsTr("The first connection over I2P can take several minutes. Do not close the app.")
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
                text: qsTr("The panel on the right shows the progress.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                focusPolicy: Theme.pressFocus
                text: qsTr("Hide")
                leftPadding: 12
                rightPadding: 12
                onClicked: root.close()
                background: Rectangle { radius: 8; color: Theme.surfaceAlt; border.color: Theme.border }
                contentItem: IconLabel { name: "close"; color: Theme.text }
            }
        }

        Item { Layout.fillWidth: true; implicitHeight: 8 }
    }
}
