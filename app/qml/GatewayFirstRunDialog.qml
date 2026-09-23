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

    signal answered()

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(520, parent ? parent.width - 24 : 520)
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }

    header: Label {
        text: "Use a private gateway?"
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }

    footer: DialogButtons {
        acceptText: root.busy ? "Checking..." : "OK"
        rejectText: "Skip"
        acceptEnabled: !root.busy && addressField.text.trim().length > 0
        onAccepted: {
            root.problem = ""
            I2p.checkAndSaveGateway(addressField.text)
        }
        onRejected: {
            I2p.skipGateway()
            root.answered()
            root.close()
        }
    }

    Connections {
        target: I2p
        function onGatewaySaved() {
            root.answered()
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
            text: "A host runs the I2P router for you; this device starts none. It sees "
                + "every address you connect to, so use one you trust. Identity keys stay on "
                + "this device. Messages, files and calls stay encrypted end to end.\n\n"
                + "Paste the address its operator gave you, or skip and this device runs its "
                + "own router. Either can be changed later in settings."
        }

        FormField {
            id: addressField
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            enabled: !root.busy
            label: "Address"
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
