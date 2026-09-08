import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import Bazarish

Rectangle {
    id: root
    property var session: null
    // An account the user switched off is opened to be read: it fetches no mail
    // and tells its own other devices nothing, so it does not write either. Said
    // here rather than found out by pressing send.
    readonly property bool switchedOff: root.session ? !root.session.online : false
    readonly property bool editing: root.session ? root.session.editing : false
    readonly property bool replying: root.session ? root.session.replying : false

    // Multi-line auto-growing input: it grows with the text up to a cap of 30% of
    // the window height, can be resized by dragging the top grip (like an html
    // textarea handle), and scrolls once the text exceeds the visible height.
    readonly property int minInputH: 38
    readonly property int maxInputH:
        Math.max(minInputH, Math.round((Window.height > 0 ? Window.height : 600) * 0.30))
    // How far the attach menu floats above the input bar.
    readonly property int kAttachMenuGap: 8

    // Prepares a picture (scale, re-encode) and sends it as an attachment. A
    // picture that cannot be read says so instead of going out as a file.
    function sendPicture(source) {
        const prepared = App.prepareImageForSend(source)
        if (prepared.length > 0 && root.session) {
            root.session.sendPicture(prepared)
        }
    }

    function sendClipboardPicture() {
        const prepared = App.prepareClipboardImage()
        if (prepared.length > 0 && root.session) {
            root.session.sendPicture(prepared)
        }
    }

    // Height set by dragging the grip (-1 means auto-size to the content).
    property int manualInputH: -1
    readonly property int autoInputH:
        Math.min(maxInputH, Math.max(minInputH, Math.ceil(input.contentHeight) + 18))
    readonly property int inputH: manualInputH >= 0
        ? Math.min(maxInputH, Math.max(minInputH, manualInputH)) : autoInputH

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight
    color: Theme.surface

    // Puts the markers of one form around what is selected in the input. The
    // selection is kept, so two forms can be applied one after the other without
    // reaching for the mouse again, and nothing is sent by pressing these - what
    // travels is the text with the markers in it, exactly as if they were typed.
    function wrapSelection(open, close) {
        const from = input.selectionStart
        const to = input.selectionEnd
        if (to <= from) {
            return
        }
        const chosen = input.text.substring(from, to)
        input.remove(from, to)
        input.insert(from, open + chosen + close)
        input.select(from + open.length, from + open.length + chosen.length)
        input.forceActiveFocus()
    }

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
                // Place the caret at the end without selecting the prefilled text,
                // so the first keystroke edits rather than replacing the message.
                input.cursorPosition = input.length
                input.deselect()
            } else {
                input.text = ""
            }
        }
        // Focus the composer when a reply starts (the field keeps its draft).
        function onReplyingChanged() {
            if (root.session.replying) {
                input.forceActiveFocus()
            }
        }
        // Opening a chat means wanting to write in it: the caret starts there
        // rather than after a click nobody should have to make.
        function onActivePeerChanged() {
            if (root.session.activePeer.length > 0) {
                Qt.callLater(function() { input.forceActiveFocus() })
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
            IconButton { iconName: "close"; onClicked: root.session.cancelEdit() }
        }

        // Reply banner (shown while composing a reply): the quoted author + preview.
        RowLayout {
            visible: root.replying
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.leftMargin: 10
            Layout.rightMargin: 6
            spacing: 8
            Rectangle { Layout.preferredWidth: 3; Layout.preferredHeight: 22; radius: 1; color: Theme.accent }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: "Reply to " + (root.session ? root.session.replyingSender : "")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: root.session && root.session.replyingText.length > 0
                    // The line being replied to, said rather than rendered.
                    text: root.session ? App.markupPlain(root.session.replyingText) : ""
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    Layout.fillWidth: true
                }
            }
            IconButton { iconName: "close"; onClicked: root.session.cancelReply() }
        }

        // What can be done to the selected fragment. Only the forms this client
        // draws are here, and only while something is selected - they act on a
        // selection and on nothing else. An address needs no button: it is
        // recognised on the other side by what it is.
        Flow {
            id: formatBar
            visible: input.activeFocus && input.selectedText.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            spacing: 6

            Repeater {
                model: [
                    { glyph: "**", name: "bold", open: "**", close: "**" },
                    { glyph: "*", name: "italic", open: "*", close: "*" },
                    { glyph: "~~", name: "strike", open: "~~", close: "~~" },
                    { glyph: "```", name: "block", open: "```", close: "```" },
                    { glyph: "!!", name: "command", open: "!!", close: "!!" }
                ]
                delegate: Button {
                    id: formatButton
                    required property var modelData
                    // The input keeps the focus and therefore the selection:
                    // these buttons act on it, and a press that took the focus
                    // would take away the thing being acted on.
                    focusPolicy: Qt.NoFocus
                    implicitHeight: 24
                    padding: 6
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: formatButton.hovered ? Theme.surfaceAlt : "transparent"
                        border.color: Theme.border
                    }
                    contentItem: Row {
                        spacing: 4
                        Label {
                            text: formatButton.modelData.glyph
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                            font.family: "monospace"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            text: formatButton.modelData.name
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    onClicked: root.wrapSelection(
                        formatButton.modelData.open, formatButton.modelData.close)
                }
            }
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
            Layout.topMargin: (Theme.barHeight - root.minInputH) / 2
            Layout.bottomMargin: (Theme.barHeight - root.minInputH) / 2
            spacing: 6

            // One way to attach anything: the clip asks what kind.
            IconButton {
                id: attachButton
                iconName: "attach"
                visible: !root.editing
                enabled: !root.switchedOff
                Layout.alignment: Qt.AlignBottom
                // Above the bar, over the conversation: opened at the cursor it
                // covered the field the user is about to type in.
                onClicked: attachMenu.popup(attachButton, 0,
                    -attachMenu.implicitHeight - root.kAttachMenuGap)
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
                        enabled: !root.switchedOff
                        placeholderText: root.switchedOff
                            ? "This account is switched off — switch it on to write"
                            : (root.editing ? "Edit message…"
                                : (root.replying ? "Reply…" : "Message…"))
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
                        // A picture in the clipboard is pasted as a picture; text
                        // pastes the way it always did.
                        Keys.onPressed: function(event) {
                            const paste = (event.key === Qt.Key_V
                                    && (event.modifiers & Qt.ControlModifier))
                                || event.key === Qt.Key_Paste
                            if (paste && App.clipboardHasImage()) {
                                root.sendClipboardPicture()
                                event.accepted = true
                            }
                        }
                    }
                }
            }

            IconButton {
                iconName: root.editing ? "check" : "send"
                tint: Theme.accent
                enabled: !root.switchedOff
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

    // Pictures only: whatever comes back is scaled and re-encoded before it is
    // sent, so a camera original does not sit in a transfer for minutes.
    FileDialog {
        id: imageDialog
        title: "Send a picture"
        nameFilters: ["Pictures (*.png *.jpg *.jpeg)", "All files (*)"]
        onAccepted: root.sendPicture(selectedFile)
    }
    FileSendDialog {
        id: sendOptions
        session: root.session
    }

    ContextMenu {
        id: attachMenu
        ContextMenuItem {
            text: "File"
            iconName: "file"
            onTriggered: fileDialog.open()
        }
        ContextMenuItem {
            // Pictures are their own thing: they are shrunk before they cross a
            // tunnel and shown in the bubble rather than listed as a file.
            text: "Picture"
            iconName: "image"
            onTriggered: imageDialog.open()
        }
        ContextMenuItem {
            text: "Voice message"
            iconName: "mic"
            onTriggered: voiceSheet.open()
        }
    }

    // Recording happens in a window of its own: what the microphone hears is
    // drawn while it records, and the take is heard before it is sent.
    VoiceRecorder {
        id: voiceSheet
        session: root.session
        parent: Overlay.overlay
        anchors.centerIn: parent
    }
}
