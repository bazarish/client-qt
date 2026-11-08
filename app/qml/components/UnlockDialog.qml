// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: dialog
    property string accountId: ""
    bottomMargin: activeFocus ? App.keyboardInset : 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    function ask(id) {
        accountId = id
        field.text = ""
        error.text = ""
        open()
    }
    function refused(reason) {
        error.text = reason
        field.text = ""
        field.forceActiveFocus()
    }
    function unlocked() {
        error.text = ""
        field.text = ""
        close()
    }
    // Not Dialog.accept(): that closes the prompt before the passphrase is known to work.
    function submit() {
        if (accountId.length > 0) {
            error.text = ""
            App.openAccount(accountId, field.text)
        }
    }
    onRejected: {
        field.text = ""
        App.cancelUnlock()
    }
    onOpened: field.forceActiveFocus()
    background: DialogFrame { }
    header: Label { text: dialog.title; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14; visible: text.length > 0 }
    footer: DialogButtons { acceptText: qsTr("Unlock"); onAccepted: dialog.submit(); onRejected: dialog.reject() }
    contentItem: ColumnLayout {
        spacing: 6
        TextField {
            id: field
            echoMode: TextInput.Password
            placeholderText: qsTr("Passphrase")
            color: Theme.text
            placeholderTextColor: Theme.textDim
            Layout.preferredWidth: 280
            onAccepted: dialog.submit()
        }
        Label {
            id: error
            visible: text.length > 0
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.preferredWidth: 280
        }
    }
}
