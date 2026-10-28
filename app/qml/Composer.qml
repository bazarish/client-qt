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
    readonly property int kPairedButtonWidth: 32
    // The Basic style's text area padding, kept so the field looks the same.
    readonly property int kFieldPadding: 6
    readonly property int kFieldLeadPadding: 10
    readonly property var kForms: [
        { glyph: "**", name: "bold", open: "**", close: "**" },
        { glyph: "*", name: "italic", open: "*", close: "*" },
        { glyph: "~~", name: "strike", open: "~~", close: "~~" },
        { glyph: "```", name: "block", open: "```", close: "```" },
        { glyph: "!!", name: "command", open: "!!", close: "!!" }
    ]

    function sendPictures(sources) {
        if (root.session) {
            root.session.sendPictureFiles(sources)
        }
    }

    function sendClipboardPicture() {
        if (root.session) {
            root.session.sendClipboardPicture()
        }
    }

    // Height set by dragging the grip (-1 means auto-size to the content).
    property int manualInputH: -1
    readonly property int autoInputH:
        Math.min(maxInputH, Math.max(minInputH, Math.ceil(input.laidOutHeight) + 18))
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
        // rather than after a click nobody should have to make. A phone would
        // raise its keyboard over the chat for it, so there the tap decides.
        function onActivePeerChanged() {
            if (root.session.activePeer.length > 0 && !App.mobile) {
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
                text: qsTr("Editing message")
                color: Theme.accent
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
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
                    text: qsTr("Reply to %1").arg(root.session ? root.session.replyingSender : "")
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
            visible: !App.mobile && input.activeFocus && input.selectedText.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            spacing: 6

            Repeater {
                model: root.kForms
                delegate: FormatChip {
                    required property var modelData
                    form: modelData
                    onClicked: root.wrapSelection(modelData.open, modelData.close)
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
                // A drag that starts on a popup over the grip is the popup's.
                enabled: !(root.Overlay.overlay && root.Overlay.overlay.visible)
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

            RowLayout {
                spacing: 0
                Layout.alignment: Qt.AlignBottom
                // One way to attach anything: the clip asks what kind.
                // Taking focus from the field would close a phone's keyboard under the finger.
                IconButton {
                    id: attachButton
                    iconName: "attach"
                    focusPolicy: Qt.NoFocus
                    visible: !root.editing
                    enabled: !root.switchedOff
                    Layout.preferredWidth: root.kPairedButtonWidth
                    // Above the bar, over the conversation: opened at the cursor it
                    // covered the field the user is about to type in.
                    onClicked: attachMenu.popup(attachButton, 0,
                        -attachMenu.implicitHeight - root.kAttachMenuGap)
                }
                IconButton {
                    id: emojiButton
                    iconName: "smile"
                    focusPolicy: Qt.NoFocus
                    enabled: !root.switchedOff
                    Layout.preferredWidth: root.kPairedButtonWidth
                    onClicked: emojiPicker.visible ? emojiPicker.close() : emojiPicker.open()
                    EmojiPicker {
                        id: emojiPicker
                        y: -height - root.kAttachMenuGap
                        recent: root.session ? root.session.recentEmoji : []
                        onPicked: function(emoji) {
                            input.insert(input.cursorPosition, emoji)
                            if (root.session) {
                                root.session.rememberEmoji(emoji)
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.inputH
                radius: 18
                color: Theme.bg
                border.color: (input.activeFocus || root.editing) ? Theme.accent : Theme.border

                ScrollView {
                    id: inputScroll
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    contentWidth: availableWidth
                    contentHeight: input.laidOutHeight + input.topPadding + input.bottomPadding

                    // A Controls TextArea drops the selection of an Android long press when the finger lifts.
                    TextEdit {
                        id: input
                        width: inputScroll.availableWidth
                        height: Math.max(laidOutHeight + topPadding + bottomPadding,
                            inputScroll.availableHeight)
                        topPadding: root.kFieldPadding
                        bottomPadding: root.kFieldPadding
                        rightPadding: root.kFieldPadding
                        leftPadding: root.kFieldLeadPadding
                        enabled: !root.switchedOff
                        color: Theme.text
                        selectionColor: inputScroll.palette.highlight
                        selectedTextColor: inputScroll.palette.highlightedText
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        // Qt 6.8 leaves out of contentHeight a composing word that wraps in an empty field.
                        readonly property real laidOutHeight: lineCount > 0
                            ? Math.max(contentHeight, positionToRectangle(length).bottom - topPadding)
                            : contentHeight
                        // What the app's selection menu offers here beyond any field's.
                        readonly property var selectionForms: root.kForms
                        function applyForm(open, close) {
                            root.wrapSelection(open, close)
                        }
                        function pasteFromMenu() {
                            if (App.clipboardHasImage()) {
                                root.sendClipboardPicture()
                            } else {
                                input.paste()
                            }
                        }
                        TapHandler {
                            enabled: App.mobile
                            acceptedDevices: PointerDevice.TouchScreen
                            onTapped: function(point) { TextSelection.tapped(input, point.position) }
                            onLongPressed: TextSelection.longPressed(input, point.position)
                        }
                        function noteTyping() {
                            if (input.activeFocus && root.session
                                    && (input.length > 0 || input.preeditText.length > 0)) {
                                root.session.typed()
                            }
                        }
                        onTextChanged: input.noteTyping()
                        onPreeditTextChanged: input.noteTyping()
                        onCursorRectangleChanged: {
                            const view = inputScroll.contentItem
                            const at = input.cursorRectangle
                            if (at.top <= view.contentY + input.topPadding) {
                                view.contentY = Math.max(0, at.top - input.topPadding)
                            } else if (at.bottom >= view.contentY + view.height - input.bottomPadding) {
                                view.contentY = Math.min(view.contentHeight - view.height,
                                    at.bottom - view.height + input.bottomPadding)
                            }
                        }

                        Label {
                            x: input.leftPadding
                            y: input.topPadding
                            width: input.width - input.leftPadding - input.rightPadding
                            visible: input.length === 0 && input.preeditText.length === 0
                            text: root.switchedOff
                                ? qsTr("This account is switched off — switch it on to write")
                                : (root.editing ? qsTr("Edit message…")
                                    : (root.replying ? qsTr("Reply…") : qsTr("Message…")))
                            color: Theme.textDim
                            font: input.font
                            elide: Text.ElideRight
                        }
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
                focusPolicy: Qt.NoFocus
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
        title: qsTr("Send pictures")
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Pictures (*.png *.jpg *.jpeg)", "All files (*)"]
        onAccepted: root.sendPictures(selectedFiles.map(String))
    }
    Connections {
        target: App
        function onPicturesPicked(uris) { root.sendPictures(uris) }
    }
    FileSendDialog {
        id: sendOptions
        session: root.session
    }

    ContextMenu {
        id: attachMenu
        ContextMenuItem {
            text: qsTr("File")
            iconName: "file"
            onTriggered: fileDialog.open()
        }
        ContextMenuItem {
            // Pictures are their own thing: they are shrunk before they cross a
            // tunnel and shown in the bubble rather than listed as a file.
            text: qsTr("Image")
            iconName: "image"
            visible: App.canPickPictures
            onTriggered: App.mobile ? App.pickPictures() : imageDialog.open()
        }
        ContextMenuItem {
            text: qsTr("Photo")
            iconName: "camera"
            onTriggered: photoSheet.open()
        }
        ContextMenuItem {
            text: qsTr("Voice message")
            iconName: "mic"
            onTriggered: voiceSheet.open()
        }
    }

    PhotoCapture {
        id: photoSheet
        onConfirmed: function(shot) { if (root.session) { root.session.sendShot(shot) } }
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
