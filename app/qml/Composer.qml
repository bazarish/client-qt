import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Rectangle {
    id: root
    property var session: null
    readonly property bool editing: root.session ? root.session.editing : false
    Layout.fillWidth: true
    implicitHeight: 58 + (editing ? 26 : 0)
    color: Theme.surface

    function send() {
        if (!root.session) {
            return
        }
        const t = input.text.trim()
        if (root.editing) {
            root.session.commitEdit(t)   // empty/unchanged just cancels
        } else if (t.length > 0) {
            root.session.sendText(t)
        }
        input.text = ""
    }

    // Mirror the controller's edit state into the input field.
    Connections {
        target: root.session
        function onEditingChanged() {
            if (root.session.editing) {
                input.text = root.session.editingText
                input.forceActiveFocus()
                input.selectAll()
            } else {
                input.text = ""
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Edit banner (shown while editing one's own message).
        RowLayout {
            visible: root.editing
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            Layout.leftMargin: 10
            Layout.rightMargin: 6
            spacing: 8
            Rectangle { Layout.preferredWidth: 3; Layout.preferredHeight: 18; radius: 1; color: Theme.accent }
            Label {
                Layout.fillWidth: true
                text: "Editing message"
                color: Theme.accent
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
            IconButton { text: "✕"; onClicked: root.session.cancelEdit() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 6

            IconButton { text: "📎"; visible: !root.editing; onClicked: fileDialog.open() }
            TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: root.editing ? "Edit message…" : "Message…"
                color: Theme.text
                selectByMouse: true
                onAccepted: root.send()
                background: Rectangle {
                    radius: 18
                    color: Theme.bg
                    border.color: root.editing ? Theme.accent : Theme.border
                }
            }
            IconButton { text: root.editing ? "✓" : "➤"; tint: Theme.accent; onClicked: root.send() }
        }
    }

    FileDialog {
        id: fileDialog
        onAccepted: root.session.sendFile(selectedFile)
    }
}
