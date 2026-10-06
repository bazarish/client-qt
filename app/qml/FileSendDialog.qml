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
    width: Math.min(380, parent ? parent.width - 24 : 380)
    padding: 0

    background: DialogFrame { }
    header: Label {
        text: qsTr("Send file")
        color: Theme.green
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: qsTr("Send")
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
            text: root.fileName
            color: Theme.text
            elide: Text.ElideMiddle
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 12
            text: qsTr("The file goes straight between you and your contact. Your contact asks "
                + "for a one-time I2P tunnel. Your app has to be online to hand the file over "
                + "when they ask.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
    }
}
