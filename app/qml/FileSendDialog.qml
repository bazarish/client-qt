// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    property var session: null
    property url fileUrl: ""

    readonly property string fileName: {
        const s = decodeURIComponent("" + root.fileUrl)
        const i = s.lastIndexOf("/")
        return i >= 0 ? s.substring(i + 1) : s
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
    padding: 0

    background: DialogFrame { }
    header: WindowHeader {
        title: qsTr("Send file")
        showClose: false
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

        MiddleElided {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            full: root.fileName
            color: Theme.text
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
