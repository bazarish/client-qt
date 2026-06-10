import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "createWizard"

    IconButton {
        text: "‹"
        font.pixelSize: 26
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        onClicked: root.StackView.view.pop()
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 420)
        spacing: 16

        Label {
            text: "New profile"
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Your identity is a key pair generated on this device."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        FormField { id: nameField; label: "Profile name"; placeholder: "e.g. Acetone" }
        FormField { id: passField; label: "Passphrase (optional, encrypts keys at rest)"; echoMode: TextInput.Password; placeholder: "leave empty for none" }
        FormField { id: confirmField; label: "Confirm passphrase"; echoMode: TextInput.Password }

        Label {
            id: errorLabel
            color: Theme.danger
            visible: text.length > 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Button {
            Layout.fillWidth: true
            text: "Create"
            enabled: nameField.text.trim().length > 0
            onClicked: {
                if (passField.text !== confirmField.text) {
                    errorLabel.text = "Passphrases do not match."
                    return
                }
                errorLabel.text = ""
                App.createProfile(nameField.text.trim(), passField.text)
            }
            background: Rectangle { radius: 10; color: parent.enabled ? Theme.accent : Theme.surfaceAlt }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
