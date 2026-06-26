import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import Bazarish

Rectangle {
    id: root
    property var session: null
    readonly property bool editing: root.session ? root.session.editing : false

    // Multi-line auto-growing input: it grows with the text up to a cap of 30% of
    // the window height, can be resized by dragging the top grip (like an html
    // textarea handle), and scrolls once the text exceeds the visible height.
    readonly property int minInputH: 38
    readonly property int maxInputH:
        Math.max(minInputH, Math.round((Window.height > 0 ? Window.height : 600) * 0.30))
    // Height set by dragging the grip (-1 means auto-size to the content).
    property int manualInputH: -1
    readonly property int autoInputH:
        Math.min(maxInputH, Math.max(minInputH, Math.ceil(input.contentHeight) + 18))
    readonly property int inputH: manualInputH >= 0
        ? Math.min(maxInputH, Math.max(minInputH, manualInputH)) : autoInputH

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight
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
        root.manualInputH = -1  // collapse back to auto-size after sending
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
        id: col
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

        // Resize grip: drag up to enlarge the composer, down to shrink it.
        // Dragging back below the minimum returns it to auto-size.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            Rectangle {
                anchors.centerIn: parent
                width: 34
                height: 3
                radius: 1.5
                color: (gripHover.hovered || gripDrag.active) ? Theme.accent : Theme.border
            }
            HoverHandler { id: gripHover; cursorShape: Qt.SizeVerCursor }
            DragHandler {
                id: gripDrag
                target: null
                xAxis.enabled: false
                yAxis.enabled: true
                property int startH: 0
                onActiveChanged: if (active) { startH = root.inputH }
                onTranslationChanged: if (active) {
                    root.manualInputH = Math.max(root.minInputH,
                        Math.min(root.maxInputH, startH - Math.round(translation.y)))
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            spacing: 6

            IconButton {
                text: "📎"
                visible: !root.editing
                Layout.alignment: Qt.AlignBottom
                onClicked: fileDialog.open()
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.inputH
                radius: 18
                color: Theme.bg
                border.color: (input.activeFocus || root.editing) ? Theme.accent : Theme.border

                ScrollView {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    TextArea {
                        id: input
                        placeholderText: root.editing ? "Edit message…" : "Message…"
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        background: null
                        // Enter sends; Shift+Enter inserts a newline. Both the main
                        // Return and the keypad Enter are handled.
                        Keys.onReturnPressed: function(event) {
                            if (event.modifiers & Qt.ShiftModifier) {
                                event.accepted = false
                            } else {
                                root.send()
                                event.accepted = true
                            }
                        }
                        Keys.onEnterPressed: function(event) {
                            if (event.modifiers & Qt.ShiftModifier) {
                                event.accepted = false
                            } else {
                                root.send()
                                event.accepted = true
                            }
                        }
                    }
                }
            }

            IconButton {
                text: root.editing ? "✓" : "➤"
                tint: Theme.accent
                Layout.alignment: Qt.AlignBottom
                onClicked: root.send()
            }
        }
    }

    // Pick a file, then choose its retention (TTL / download count) before sending.
    FileDialog {
        id: fileDialog
        onAccepted: { sendOptions.fileUrl = selectedFile; sendOptions.open() }
    }
    FileSendDialog {
        id: sendOptions
        session: root.session
    }
}
