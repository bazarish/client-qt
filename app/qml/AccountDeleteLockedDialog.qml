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
    background: DialogFrame { destructive: true }
    header: Label {
        text: qsTr("This account is locked")
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: qsTr("Unlock and delete")
        rejectText: qsTr("Cancel")
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
            text: qsTr("\u201c%1\u201d is encrypted, and only the key inside can tell the server to end the account.").arg(root.accountName)
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 4
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            text: qsTr("Delete this device's copy only")
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
            text: qsTr("Without the passphrase nothing can be said to the server. The account would stay there with no key left to end it.")
        }
    }
}
