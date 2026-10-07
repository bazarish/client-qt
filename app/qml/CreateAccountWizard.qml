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
        iconName: "back"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        onClicked: root.StackView.view.pop()
    }

    // The same gear as on the account list: a client waiting on I2P needs the
    // router status, and there is no account open to reach it through.
    IconButton {
        iconName: "gear"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        onClicked: appSettings.open()
    }
    RouterStatusPage { id: routerStatus; onBack: { routerStatus.close(); appSettings.open() } }
    AppSettingsPage {
        id: appSettings
        parent: Overlay.overlay
        anchors.centerIn: parent
        onShowRouterStatus: routerStatus.open()
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 420)
        spacing: 16

        Label {
            text: qsTr("New account")
            color: Theme.green
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Your identity is a key pair generated on this device.")
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        FormField { id: nameField; label: qsTr("Account name"); placeholder: qsTr("e.g. Mr. Who"); maximumLength: 64 }
        FormField { id: passField; label: qsTr("Passphrase (optional, encrypts keys at rest)"); echoMode: TextInput.Password; placeholder: qsTr("leave empty for none") }
        FormField { id: confirmField; label: qsTr("Confirm passphrase"); echoMode: TextInput.Password }

        Label {
            id: errorLabel
            color: Theme.danger
            visible: text.length > 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Button {
            id: createButton
            Layout.fillWidth: true
            text: qsTr("Create")
            hoverEnabled: true
            enabled: nameField.text.trim().length > 0
            onClicked: {
                if (passField.text !== confirmField.text) {
                    errorLabel.text = qsTr("Passphrases do not match.")
                    return
                }
                errorLabel.text = ""
                App.createAccount(nameField.text.trim(), passField.text)
            }
            background: Rectangle { radius: 10; color: !parent.enabled ? Theme.surfaceAlt : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: IconLabel {
                name: "plus"
                color: createButton.enabled ? Theme.accentText : Theme.textDim
            }
        }

        // Restore everything (keys, routing and contacts) from a .bazarish backup
        // instead of creating a fresh identity. Uses the name and passphrase above.
        // The display name is restored from the backup itself, so a name is not
        // required here; if one is typed it only picks the on-disk account id.
        Button {
            Layout.fillWidth: true
            text: qsTr("Restore from backup")
            hoverEnabled: true
            onClicked: {
                if (passField.text !== confirmField.text) {
                    errorLabel.text = qsTr("Passphrases do not match.")
                    return
                }
                errorLabel.text = ""
                restoreDialog.open()
            }
            background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
            contentItem: IconLabel { name: "folder"; color: Theme.accent }
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Connect this device online")
            hoverEnabled: true
            onClicked: {
                if (passField.text !== confirmField.text) {
                    errorLabel.text = qsTr("Passphrases do not match.")
                    return
                }
                errorLabel.text = ""
                pairThisDeviceSheet.open()
            }
            background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
            contentItem: IconLabel { name: "devices"; color: Theme.accent }
        }
    }

    PairThisDeviceSheet {
        id: pairThisDeviceSheet
        atRestPassphrase: passField.text
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
        title: qsTr("Backup password")
        // name + at-rest passphrase come from the wizard fields; this asks only
        // for the password the backup file was sealed with.
        onAccepted: App.importAccount(nameField.text.trim(), root.pendingBackupFile,
            backupPass.text, passField.text)
        background: DialogFrame { }
        header: Label { text: qsTr("Backup password"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { onAccepted: backupPassDialog.accept(); onRejected: backupPassDialog.reject() }
        contentItem: TextField {
            id: backupPass
            echoMode: TextInput.Password
            placeholderText: qsTr("password the backup was saved with")
            color: Theme.text
            placeholderTextColor: Theme.textDim
            implicitWidth: 280
            onAccepted: backupPassDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: backupPass.activeFocus ? Theme.accent : Theme.border }
        }
    }
}
