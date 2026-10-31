// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Item {
    id: root
    objectName: "createWizard"

    property string pairLink: ""

    property string pendingBackupFile: ""

    IconButton {
        iconName: "back"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        onClicked: root.StackView.view.pop()
    }

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

    ScrollColumn {
        z: -1
        anchors.fill: parent
        centered: true
        columnWidth: Math.min(availableWidth - 2 * Theme.spacing, 420)
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
            focusPolicy: Theme.pressFocus
            Layout.fillWidth: true
            text: qsTr("Create")
            hoverEnabled: true
            enabled: nameField.displayText.trim().length > 0
            onClicked: {
                if (passField.text !== confirmField.text) {
                    errorLabel.text = qsTr("Passphrases do not match.")
                    return
                }
                errorLabel.text = ""
                App.createAccount(nameField.text.trim(), passField.text)
            }
            background: Rectangle { radius: 10; color: !parent.enabled ? Theme.surfaceAlt : (parent.hovered ? Theme.accentHover : Theme.accent) }
            contentItem: IconLabel {
                name: "plus"
                color: createButton.enabled ? Theme.accentText : Theme.textDim
            }
        }

        Button {
            focusPolicy: Theme.pressFocus
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
            focusPolicy: Theme.pressFocus
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
        prefillLink: root.pairLink
    }

    FileDialog {
        id: restoreDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Bazarish backup (*.bazarish)", "All files (*)"]
        onAccepted: { root.pendingBackupFile = selectedFile; backupPassDialog.open() }
    }

    Dialog {
        id: backupPassDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        title: qsTr("Backup password")
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
