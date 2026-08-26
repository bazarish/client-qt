import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// A locked profile: the key inside it is the only thing that can end the account
// on its server, so unlocking is the difference between deleting the account and
// deleting this device's copy of it. Both are offered, and what the second one
// leaves behind is said plainly rather than discovered later.
Dialog {
    id: root
    property string accountId: ""
    property string accountName: ""
    signal unlockRequested(string id)
    signal localOnlyRequested(string id)

    function show(id, name) { accountId = id; accountName = name; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(400, parent ? parent.width - 24 : 400)
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
    header: Label {
        text: "This account is locked"
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Unlock and delete"
        rejectText: "Cancel"
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.unlockRequested(root.accountId)
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 0
            wrapMode: Text.Wrap
            color: Theme.text
            text: "\"" + root.accountName + "\" is encrypted, and the key inside it is the only "
                + "thing that can tell its server to end the account. Unlock it and the account "
                + "is deleted everywhere."
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 4
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            text: "Delete this device's copy only"
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: {
                    root.localOnlyRequested(root.accountId)
                    root.close()
                }
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
            text: "Without the passphrase nothing can be said to the server. If it still holds "
                + "this account, it keeps it - address, mailbox and all - and there will be no "
                + "key left to end it."
        }
    }
}
