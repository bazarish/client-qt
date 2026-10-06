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
    // "Remove from this device": the account stays on its server and on the
    // user's other devices; this device stops being one of them.
    signal localOnlyRequested(string id)

    function show(id, name) { accountId = id; accountName = name ? name : ""; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(360, parent ? parent.width - 24 : 360)
    title: qsTr("Delete account")
    footer: DialogButtons {
        acceptText: qsTr("Delete everywhere")
        danger: true
        // The third answer: neither ending the account nor leaving things as
        // they are.
        showCancel: true
        cancelOnOwnLine: true
        cancelText: qsTr("Remove from this device only")
        onCancelled: { root.close(); root.localOnlyRequested(root.accountId) }
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.confirmed(root.accountId)
    // Destructive: brightest-neon outline, dark surface, light text.
    background: DialogFrame { destructive: true }
    header: Label {
        text: qsTr("Delete account")
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    contentItem: Label {
        text: (root.accountName.length > 0
                ? qsTr("Permanently delete \u201c%1\u201d? ").arg(root.accountName)
                : qsTr("Permanently delete this account? "))
            + qsTr("The account ends on the server and goes from this device. This cannot be undone.\n\nRemoving from this device only forgets this device: the account and its mail stay.")
        color: Theme.text
        wrapMode: Text.Wrap
    }
}
