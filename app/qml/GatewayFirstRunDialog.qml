// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root

    property bool busy: I2p.gatewayChecking
    property string problem: ""

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(520, parent ? parent.width - 24 : 520)
    margins: Theme.spacing
    bottomMargin: Theme.spacing + (activeFocus ? App.keyboardInset : 0)
    background: DialogFrame { destructive: true }

    header: Label {
        text: qsTr("Use an external gateway?")
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }

    footer: DialogButtons {
        acceptText: root.busy ? qsTr("Checking\u2026") : qsTr("OK")
        rejectText: qsTr("Skip")
        acceptEnabled: !root.busy && addressField.displayText.trim().length > 0
        onAccepted: {
            root.problem = ""
            I2p.checkAndSaveGateway(addressField.text)
        }
        onRejected: {
            I2p.skipGateway()
            root.close()
        }
    }

    Connections {
        target: I2p
        enabled: root.opened
        function onGatewaySaved() {
            I2p.useGateway(true)
            root.close()
        }
        function onGatewayRefused(reason) {
            root.problem = reason
        }
    }

    contentItem: ScrollColumn {
        spacing: 8

        Label {
            Layout.fillWidth: true
            padding: 14
            bottomPadding: 0
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("Paste the address its operator gave you, or skip. The app will run its built-in router.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            FormField {
                id: addressField
                Layout.fillWidth: true
                enabled: !root.busy
                label: qsTr("Address")
                placeholder: "https://host/path#token"
                inputField.onAccepted:
                    if (!root.busy && addressField.text.trim().length > 0) {
                        I2p.checkAndSaveGateway(addressField.text)
                    }
            }
            QrScanButton {
                enabled: !root.busy
                Layout.alignment: Qt.AlignBottom
                onDecoded: function(text) { addressField.text = text }
            }
        }

        Label {
            visible: root.problem.length > 0
            Layout.fillWidth: true
            leftPadding: 14
            rightPadding: 14
            bottomPadding: 4
            wrapMode: Text.Wrap
            text: root.problem
            color: Theme.danger
            font.weight: Font.Medium
        }
    }
}
