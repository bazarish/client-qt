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
        text: "Your server serves another address"
        color: Theme.warn
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Publish this one"
        rejectText: "Later"
        showCancel: true
        cancelText: "New address"
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
            text: "Your server is serving " + root.servedHost + ", and this device holds "
                + (root.ourHost.length > 0 ? root.ourHost : "no address")
                + ". None of your other devices answered with the keys to the served one, so "
                + "this device cannot receive anything on it."
        }
        Label {
            width: parent.width - 28
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            text: "Publishing this device's address (or a new one) makes your server serve it "
                + "instead. Contacts still holding the old address cannot reach you until they "
                + "hear from you again - which happens the next time you write to each of them. "
                + "If another device of yours is simply offline, leave this until it is on."
        }
    }
}
