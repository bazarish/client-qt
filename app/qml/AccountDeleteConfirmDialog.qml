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
    property bool primary: false
    property bool otherPrimaries: false
    signal confirmed(string id)
    signal localOnlyRequested(string id)

    function show(id, name, primaryDevice, otherPrimaryDevices) {
        accountId = id
        accountName = name ? name : ""
        primary = primaryDevice === true
        otherPrimaries = otherPrimaryDevices === true
        open()
    }

    function chosen(everywhere) {
        if (!primary) {
            if (everywhere) {
                root.confirmed(root.accountId)
            } else {
                root.localOnlyRequested(root.accountId)
            }
            return
        }
        sure.everywhere = everywhere
        sure.open()
    }

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
        onCancelled: { root.close(); root.chosen(false) }
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.chosen(!root.localOnly)
    background: DialogFrame { destructive: true }
    header: WindowHeader {
        title: root.title
        showClose: false
    }
    contentItem: Column {
        spacing: Theme.spacing
        Label {
            width: parent.width
            visible: root.accountName.length > 0
            text: root.accountName
            color: Theme.text
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            width: parent.width
            text: root.localOnly
                ? qsTr("This device forgets the account. The account and its mail stay on the server.")
                : qsTr("Permanently delete this account? ")
                    + qsTr("The account ends on the server and goes from this device. This cannot be undone.\n\nRemoving from this device only, the server forgets just this device. The server keeps receiving mail.")
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: sure
        property bool everywhere: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: root.width
        title: qsTr("Are you sure?")
        footer: DialogButtons {
            acceptText: qsTr("Yes")
            rejectText: qsTr("No")
            danger: true
            onAccepted: sure.accept()
            onRejected: sure.reject()
        }
        onAccepted: {
            if (sure.everywhere) {
                root.confirmed(root.accountId)
            } else {
                root.localOnlyRequested(root.accountId)
            }
        }
        background: DialogFrame { destructive: true }
        header: WindowHeader {
            title: sure.title
            showClose: false
        }
        contentItem: Label {
            text: sure.everywhere
                ? qsTr("The account stops working on all its devices. This cannot be undone.")
                : (root.otherPrimaries
                    ? qsTr("The account key stays only on the other primary devices")
                    : qsTr("This is the only primary device, and the account key goes with it. Without the key no device can be added, and within 30 days the account stops receiving messages. Back up the account to keep the key."))
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
}
