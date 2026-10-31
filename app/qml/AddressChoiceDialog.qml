// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Dialog {
    id: root
    property string servedHost: ""
    property string ourHost: ""

    function show(served, ours) { servedHost = served; ourHost = ours; open() }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(420, parent ? parent.width - 24 : 420)
    background: DialogFrame { }
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
