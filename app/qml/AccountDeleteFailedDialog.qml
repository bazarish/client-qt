import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// A deletion that did not go through, in one of two ways: the server never
// answered, or the profile itself would not open. Nothing has been removed
// either way, and the choice between trying later and walking away belongs to
// the user - but only one of the two is worth trying again.
Dialog {
    id: root
    property string accountId: ""
    property string reason: ""
    signal retryRequested(string id)
    signal localOnlyRequested(string id)

    // The profile could not be opened at all, so there was nothing to ask the
    // server with and nothing a second attempt would do differently.
    property bool profileNotOpened: false

    function show(id, error, notOpened) {
        accountId = id
        reason = error
        profileNotOpened = notOpened === true
        open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(400, parent ? parent.width - 24 : 400)
    background: DialogFrame { destructive: true }
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
        // Nothing about a profile that will not open changes between attempts.
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
                ? "This profile could not be opened, so the account on its server was "
                  + "not ended:"
                : "Your server did not answer, so the account was not ended:"
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
            text: root.profileNotOpened
                ? "Nothing was deleted. The key that ends the account is inside this "
                  + "profile, and this build cannot read it."
                : "Nothing was deleted. Try again when it is reachable - this profile "
                  + "holds the only key that can ask it to."
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
