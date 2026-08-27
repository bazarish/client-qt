import QtQuick
import QtQuick.Controls
import Bazarish

// The transport cannot be swapped while the application runs: the embedded I2P
// engine can be started once per process and no more. So the choice is saved for
// the next start, and the only question is whether to close the application now.
Dialog {
    id: root
    // Which way the switch was moved, so the text can say what will happen.
    property bool samOn: false
    // true = save and close now, false = save and stay on this transport until
    // the application is next started. Both answers save, as the proxy dialog does.
    signal answered(bool closeNow)

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(420, parent ? parent.width - 24 : 420)
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
    header: Label {
        text: "Close the application?"
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Save and close"
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
        text: root.samOn
            ? "Traffic will go through the router at the address above instead of the one "
              + "inside this application. Which of the two carries it is decided when the "
              + "application starts, so this takes effect the next time it runs. Closing "
              + "now ends every connection; saving only leaves everything as it is."
            : "Traffic will go through the I2P router inside this application again. Which "
              + "of the two carries it is decided when the application starts, so this "
              + "takes effect the next time it runs. Closing now ends every connection; "
              + "saving only leaves everything as it is."
    }
}
