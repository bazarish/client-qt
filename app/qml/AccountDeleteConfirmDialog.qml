// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Dialog {
    id: root
    property string accountId: ""
    property string accountName: ""
    signal confirmed(string id)
    signal localOnlyRequested(string id)

    function show(id, name) { accountId = id; accountName = name ? name : ""; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
    title: qsTr("Delete account")
    footer: DialogButtons {
        acceptText: qsTr("Delete everywhere")
        danger: true
        showCancel: true
        cancelOnOwnLine: true
        cancelText: qsTr("Remove from this device only")
        onCancelled: { root.close(); root.localOnlyRequested(root.accountId) }
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.confirmed(root.accountId)
    background: DialogFrame { destructive: true }
    header: WindowHeader {
        title: root.title
        showClose: false
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
