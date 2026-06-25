import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Item {
    id: root
    objectName: "createWizard"

    // The chosen .bazarish file while the backup password is being entered.
    property string pendingBackupFile: ""

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
            color: Theme.green
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

        FormField { id: nameField; label: "Profile name"; placeholder: "e.g. Mr. Who" }
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
            hoverEnabled: true
            enabled: nameField.text.trim().length > 0
            onClicked: {
                if (passField.text !== confirmField.text) {
                    errorLabel.text = "Passphrases do not match."
                    return
                }
                errorLabel.text = ""
                App.createProfile(nameField.text.trim(), passField.text)
            }
            background: Rectangle { radius: 10; color: !parent.enabled ? Theme.surfaceAlt : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: Label { text: parent.text; color: parent.enabled ? Theme.accentText : Theme.textDim; horizontalAlignment: Text.AlignHCenter }
        }

        // Restore everything (keys, routing and contacts) from a .bazarish backup
        // instead of creating a fresh identity. Uses the name and passphrase above.
        // The display name is restored from the backup itself, so a name is not
        // required here; if one is typed it only picks the on-disk profile id.
        Button {
            Layout.fillWidth: true
            text: "Restore from backup…"
            hoverEnabled: true
            onClicked: {
                if (passField.text !== confirmField.text) {
                    errorLabel.text = "Passphrases do not match."
                    return
                }
                errorLabel.text = ""
                restoreDialog.open()
            }
            background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
            contentItem: Label { text: parent.text; color: parent.enabled ? Theme.accent : Theme.textDim; horizontalAlignment: Text.AlignHCenter }
        }
    }

    FileDialog {
        id: restoreDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Bazarish backup (*.bazarish)", "All files (*)"]
        onAccepted: { root.pendingBackupFile = selectedFile; backupPassDialog.open() }
    }

    Dialog {
        id: backupPassDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Backup password"
        // name + at-rest passphrase come from the wizard fields; this asks only
        // for the password the backup file was sealed with.
        onAccepted: App.importProfile(nameField.text.trim(), root.pendingBackupFile,
            backupPass.text, passField.text)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Backup password"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { onAccepted: backupPassDialog.accept(); onRejected: backupPassDialog.reject() }
        contentItem: TextField {
            id: backupPass
            echoMode: TextInput.Password
            placeholderText: "password the backup was saved with"
            color: Theme.text
            placeholderTextColor: Theme.textDim
            implicitWidth: 280
            onAccepted: backupPassDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: backupPass.activeFocus ? Theme.accent : Theme.border }
        }
    }
}
