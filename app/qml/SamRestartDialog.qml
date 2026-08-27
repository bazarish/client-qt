import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// The transport cannot be swapped while the application runs: the embedded I2P
// engine can be started once per process and no more. So the choice is saved for
// the next start, and the only question is whether to close the application now.
Dialog {
    id: root
    // Which way the switch was moved, so the text can say what will happen.
    property bool samOn: false
    // Where the router is said to be, so turning this on can be answered with
    // whether anything is actually there.
    property string host: ""
    property int port: 0
    // The answer, once asked: a check is only meaningful when turning it on.
    property bool checked_: false
    property bool checkOk: false

    onOpened: {
        checked_ = samOn
        if (samOn) {
            checkOk = I2p.samReachable(host, port)
        }
    }
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
    contentItem: ColumnLayout {
        spacing: 8
        Label {
            Layout.fillWidth: true
            padding: 14
            bottomPadding: 0
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
        // What is at that address right now. Said before the user commits to a
        // restart, because after one there is nothing to fall back to.
        Label {
            visible: root.checked_
            Layout.fillWidth: true
            leftPadding: 14
            rightPadding: 14
            bottomPadding: 4
            text: root.checkOk ? "SAM check OK" : "SAM check failed"
            color: root.checkOk ? Theme.success : Theme.danger
            font.weight: Font.Medium
        }
    }
}
