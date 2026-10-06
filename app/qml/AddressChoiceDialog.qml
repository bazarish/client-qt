import QtQuick
import QtQuick.Controls
import Bazarish

// Asked when the server is serving an address this device has no keys for and no
// other device of the account answered with them - the state a second device used
// to create silently by publishing an address of its own. Nothing is published
// until this is answered, because either answer takes the account's address away
// from the contacts holding the served one.
Dialog {
    id: root
    property string servedHost: ""
    property string ourHost: ""

    function show(served, ours) { servedHost = served; ourHost = ours; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(420, parent ? parent.width - 24 : 420)
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.warn }
    header: Label {
        text: qsTr("Your server serves another address")
        color: Theme.warn
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: qsTr("Publish this one")
        rejectText: qsTr("Later")
        showCancel: true
        cancelText: qsTr("New address")
        onAccepted: { root.close(); if (App.session) { App.session.keepThisDeviceAddress() } }
        onCancelled: { root.close(); if (App.session) { App.session.useFreshAddress() } }
        onRejected: root.close()
    }
    contentItem: Column {
        spacing: 8
        padding: 14
        Label {
            width: parent.width - 28
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("Your server is serving %1; this device holds %2. No other device answered with the keys to the served one.")
                .arg(root.servedHost)
                .arg(root.ourHost.length > 0 ? root.ourHost : qsTr("no address"))
        }
        Label {
            width: parent.width - 28
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            text: qsTr("Your server will serve this address instead. Contacts holding the old one cannot write to you until your next message. If another device of yours is offline, wait until it is on.")
        }
    }
}
