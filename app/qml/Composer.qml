import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Rectangle {
    id: root
    property var session: null
    Layout.fillWidth: true
    implicitHeight: 58
    color: Theme.surface

    function send() {
        const t = input.text.trim()
        if (t.length > 0 && root.session) {
            root.session.sendText(t)
            input.text = ""
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6

        IconButton { text: "📎"; onClicked: fileDialog.open() }
        TextField {
            id: input
            Layout.fillWidth: true
            placeholderText: "Message…"
            color: Theme.text
            selectByMouse: true
            onAccepted: root.send()
            background: Rectangle { radius: 18; color: Theme.bg; border.color: Theme.border }
        }
        IconButton { text: "➤"; tint: Theme.accent; onClicked: root.send() }
    }

    FileDialog {
        id: fileDialog
        onAccepted: root.session.sendFile(selectedFile)
    }
}
