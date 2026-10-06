import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Asked once, before anything starts an I2P router, because the answer decides
// which one starts. A gateway is a host somebody already trusted runs; with one,
// this device speaks https to it and never builds a tunnel of its own. Without
// one, the router inside this application comes up as it always did.
Dialog {
    id: root

    property bool busy: I2p.gatewayChecking
    property string problem: ""

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(520, parent ? parent.width - 24 : 520)
    background: DialogFrame { destructive: true }

    header: Label {
        text: qsTr("Use a private gateway?")
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }

    footer: DialogButtons {
        acceptText: root.busy ? qsTr("Checking\u2026") : qsTr("OK")
        rejectText: qsTr("Skip")
        acceptEnabled: !root.busy && addressField.text.trim().length > 0
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
        function onGatewaySaved() {
            // Asked before any engine is up, so choosing here is what this run
            // will use rather than a change to anything: saving the address and
            // using it are one act at this door, and two everywhere else.
            I2p.useGateway(true)
            root.close()
        }
        function onGatewayRefused(reason) {
            root.problem = reason
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Label {
            Layout.fillWidth: true
            padding: 14
            bottomPadding: 0
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("A host runs the I2P router for you. It sees every address you connect to. Use one you trust.\n\nPaste the address its operator gave you, or skip. The app will run its built-in router.")
        }

        FormField {
            id: addressField
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            enabled: !root.busy
            label: qsTr("Address")
            placeholder: "https://host/path#token"
            inputField.onAccepted:
                if (!root.busy && addressField.text.trim().length > 0) {
                    I2p.checkAndSaveGateway(addressField.text)
                }
        }

        // Why it was refused. An address that does not answer is not saved:
        // there is nothing useful to do with one, and keeping it would move the
        // failure somewhere this dialog cannot show it.
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
