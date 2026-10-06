import QtQuick
import QtQuick.Controls
import Bazarish

// The router reads the proxy setting as its transports come up, so a change
// reaches a running router only by restarting it. Saving and applying are
// therefore two different things, and this is where the user says which they
// want - both answers save, only one restarts.
Dialog {
    id: root
    // true = save and restart the router now, false = save only.
    signal answered(bool restartNow)

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(400, parent ? parent.width - 24 : 400)
    background: DialogFrame { destructive: true }
    header: Label {
        text: qsTr("Restart the router?")
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: qsTr("Save and restart")
        rejectText: qsTr("Save only")
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.answered(true)
    onRejected: root.answered(false)
    contentItem: Label {
        padding: 14
        wrapMode: Text.Wrap
        color: Theme.text
        text: qsTr("Restarting now rebuilds the tunnels, which takes a minute or two. Saving applies the setting at the next start.")
    }
}
