import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 440)
        spacing: 14

        Label {
            text: "Connect to a server"
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Your profile needs a serving server to send and receive. "
                + "Everything stays end-to-end encrypted."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        FormField { id: hostField; label: "Facade host"; text: "127.0.0.1" }
        FormField { id: portField; label: "Port"; placeholder: "e.g. 18559" }
        FormField { id: pathField; label: "Base path (optional)" }
        FormField { id: fpField; label: "Server fingerprint" }

        Button {
            Layout.fillWidth: true
            text: "Connect & subscribe"
            enabled: hostField.text.length > 0 && portField.text.length > 0 && fpField.text.length > 0
            onClicked: root.session.connectServer(hostField.text.trim(),
                parseInt(portField.text), pathField.text.trim(), fpField.text.trim())
            background: Rectangle { radius: 10; color: parent.enabled ? Theme.accent : Theme.surfaceAlt }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
