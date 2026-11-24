// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    property string accountId: ""
    property string reason: ""
    signal retryRequested(string id)
    signal localOnlyRequested(string id)

    property bool profileNotOpened: false
    property bool leaving: false

    function show(id, error, notOpened, leavingAccount) {
        accountId = id
        reason = error
        profileNotOpened = notOpened === true
        leaving = leavingAccount === true
        open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
    background: DialogFrame { destructive: true }
    header: WindowHeader {
        title: qsTr("The account was not deleted")
        showClose: false
    }
    footer: DialogButtons {
        acceptText: qsTr("Try again")
        rejectText: qsTr("Close")
        acceptEnabled: !root.profileNotOpened
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.retryRequested(root.accountId)
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 0
            wrapMode: Text.Wrap
            color: Theme.text
            text: root.profileNotOpened
                ? qsTr("This profile could not be opened, so the account on its server was "
                  + "not ended:")
                : qsTr("Your server did not answer, so the account was not ended:")
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            maximumLineCount: 5
            elide: Text.ElideRight
            color: Theme.textDim
            text: root.reason
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: Theme.text
            text: root.profileNotOpened
                ? qsTr("Nothing was deleted. The key that ends the account is inside this "
                  + "profile, and this build cannot read it.")
                : qsTr("Nothing was deleted. Try again when it is reachable - this profile "
                  + "holds the only key that can ask it to.")
        }
        MenuButton {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            text: qsTr("Remove from this device anyway")
            danger: true
            onClicked: {
                root.localOnlyRequested(root.accountId)
                root.close()
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 10
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            text: qsTr("The account would stay on the server with no key left to end it")
        }
    }
}
