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
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
    header: Label {
        text: "Restart the router?"
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Save and restart"
        rejectText: "Save only"
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    onAccepted: root.answered(true)
    onRejected: root.answered(false)
    contentItem: Label {
        padding: 14
        wrapMode: Text.Wrap
        color: Theme.text
        text: "The proxy setting is read by the embedded I2P router as it starts, so it "
            + "takes effect when that router restarts. Restarting it now drops the tunnels "
            + "and builds them again, which takes a minute or two; saving only leaves the "
            + "router as it is and applies the setting the next time it starts."
    }
}
