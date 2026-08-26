import QtQuick
import QtQuick.Controls
import Bazarish

// The confirmation before an account is ended. One wording in one place: the two
// ways into it - the account list and the account's own settings - are asking
// the same question, and the answer costs the same either way.
Dialog {
    id: root
    property string accountId: ""
    // Empty when the caller is inside the account already and naming it would
    // only repeat what the window is showing.
    property string accountName: ""
    signal confirmed(string id)

    function show(id, name) { accountId = id; accountName = name ? name : ""; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(360, parent ? parent.width - 24 : 360)
    title: "Delete account"
    footer: DialogButtons {
        acceptText: "Delete"
        danger: true
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.confirmed(root.accountId)
    // Destructive: brightest-neon outline, dark surface, light text.
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
    header: Label {
        text: "Delete account"
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    contentItem: Label {
        text: (root.accountName.length > 0
                ? "Permanently delete \"" + root.accountName + "\"? "
                : "Permanently delete this account? ")
            + "Your server ends the account - its address, its mailbox and everything it "
            + "holds - and then the profile and its messages go from this device. Make sure "
            + "you have a backup if you might need it again. This cannot be undone."
        color: Theme.text
        wrapMode: Text.Wrap
    }
}
