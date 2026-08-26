import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// A deletion the server never answered. Nothing has been removed: the profile is
// what holds the key that can ask again, so the choice between trying later and
// walking away belongs to the user.
Dialog {
    id: root
    property string accountId: ""
    property string reason: ""
    signal retryRequested(string id)
    signal localOnlyRequested(string id)

    function show(id, error) { accountId = id; reason = error; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(400, parent ? parent.width - 24 : 400)
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
    header: Label {
        text: "The account was not deleted"
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Try again"
        rejectText: "Close"
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
            text: "Your server did not answer, so the account was not ended:"
        }
        // The reason comes from the transport and can be any length; past a few
        // lines it would push the buttons out of a small window, and the whole of
        // it is in the log either way.
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
            text: "Nothing was deleted. Try again when it is reachable - this profile holds "
                + "the only key that can ask it to."
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 4
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            text: "Remove from this device anyway"
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
            text: "The account would go on existing on the server - address, mailbox and all - "
                + "with no key left anywhere to end it."
        }
    }
}
