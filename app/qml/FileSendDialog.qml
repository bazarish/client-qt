import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Shown right after a file is picked. There is nothing to configure: a file goes
// straight to the recipient over a one-time I2P destination, encrypted with a key
// only they hold, and the message that passes through the servers carries the
// name, the size and where to fetch it - never the bytes. So this asks for a
// confirmation and says what is about to happen.
Dialog {
    id: root
    property var session: null
    // The picked file (a file:// URL). Set this, then open().
    property url fileUrl: ""

    readonly property string fileName: {
        const s = decodeURIComponent("" + root.fileUrl)
        const i = s.lastIndexOf("/")
        return i >= 0 ? s.substring(i + 1) : s
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: 380
    padding: 0

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
    header: Label {
        text: "Send file"
        color: Theme.green
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Send"
        onAccepted: root.accept()
        onRejected: root.reject()
    }

    onAccepted: {
        if (root.session && ("" + root.fileUrl).length > 0) {
            root.session.sendFile(root.fileUrl)
        }
        root.fileUrl = ""
    }
    onRejected: root.fileUrl = ""

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            text: "📎  " + root.fileName
            color: Theme.text
            elide: Text.ElideMiddle
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 12
            text: "Sent straight to this contact over a one-time I2P address, encrypted with a "
                + "key only they have. Your server only carries the offer — the name, the size "
                + "and where to fetch it. Keep the app open until the transfer finishes: it is "
                + "device to device, so nothing holds the file for them in the meantime."
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
    }
}
