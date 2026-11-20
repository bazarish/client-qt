// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Dialog {
    id: root
    property string accountId: ""
    property string accountName: ""
    // A secondary device cannot end the account: it only forgets it.
    property bool localOnly: false
    signal confirmed(string id)
    signal localOnlyRequested(string id)

    function show(id, name) { accountId = id; accountName = name ? name : ""; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
    title: qsTr("Delete account")
    footer: DialogButtons {
        acceptText: root.localOnly ? qsTr("Remove from this device only") : qsTr("Delete everywhere")
        danger: true
        showCancel: !root.localOnly
        cancelOnOwnLine: !root.localOnly
        rejectBelow: root.localOnly
        cancelText: qsTr("Remove from this device only")
        onCancelled: { root.close(); root.localOnlyRequested(root.accountId) }
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.localOnly ? root.localOnlyRequested(root.accountId) : root.confirmed(root.accountId)
    background: DialogFrame { destructive: true }
    header: WindowHeader {
        title: root.title
        showClose: false
    }
    contentItem: Label {
        text: root.localOnly
            ? qsTr("This device forgets the account. The account and its mail stay on the server.")
            : (root.accountName.length > 0
                ? qsTr("Permanently delete \u201c%1\u201d? ").arg(root.accountName)
                : qsTr("Permanently delete this account? "))
                + qsTr("The account ends on the server and goes from this device. This cannot be undone.\n\nRemoving from this device only forgets this device: the account and its mail stay.")
        color: Theme.text
        wrapMode: Text.Wrap
    }
}
