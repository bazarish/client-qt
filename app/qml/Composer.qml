// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import Bazarish

Rectangle {
    id: root
    property var session: null
    readonly property bool switchedOff: root.session ? !root.session.online : false
    readonly property bool editing: root.session ? root.session.editing : false
    readonly property bool replying: root.session ? root.session.replying : false

    readonly property int minInputH: 38
    readonly property int maxInputH:
        Math.max(minInputH, Math.round((Window.height > 0 ? Window.height : 600) * 0.30))
    readonly property int kAttachMenuGap: 8
    readonly property int kPairedButtonWidth: 40
    readonly property int kBarIconSize: 27
    readonly property real kSendPressGrowth: 1.1
    readonly property int kSendPressMs: 90
    readonly property int kMentionListWidth: 260
    readonly property int kMentionRowPadding: 12
    readonly property int kMentionRowSpacing: 7
    // The Basic style's text area padding, kept so the field looks the same.
    readonly property int kFieldPadding: 6
    readonly property var kForms: [
        { name: qsTr("bold"), open: "**", close: "**", key: Qt.Key_B },
        { name: qsTr("italic"), open: "*", close: "*", key: Qt.Key_I },
        { name: qsTr("strikethrough"), open: "~~", close: "~~" },
        { name: qsTr("code"), open: "```", close: "```" },
        { name: qsTr("command"), open: "!!", close: "!!" }
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

    readonly property bool pickingEmoji: emojiPicker.visible

    readonly property int inputH:
        Math.min(maxInputH, Math.max(minInputH, Math.ceil(input.laidOutHeight) + 18))

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight
    color: Theme.surface

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

    // The word still being composed on a phone counts: the choices follow every letter.
    readonly property var mention: root.session && root.session.activeMembers.length > 0
        && input.activeFocus
        ? root.session.mentionAt(input.text.substring(0, input.cursorPosition) + input.preeditText)
        : ({})
    readonly property var mentionChoices: root.mention.choices || []

    function insertMention(name) {
        const written = root.session.mentionText(name) + " "
        const at = input.cursorPosition
        input.insert(at, written)
        input.cursorPosition = at + written.length
        input.forceActiveFocus()
    }

    function completeMention(name) {
        const from = root.mention.from
        const written = root.session.mentionText(name) + " "
        input.remove(from, input.cursorPosition)
        input.insert(from, written)
        input.cursorPosition = from + written.length
    }

    property string draftPeer: ""
    property bool restoring: false

    function showText(text) {
        root.restoring = true
        input.text = text
        input.cursorPosition = input.length
        root.restoring = false
    }

    function send() {
        if (!root.session) {
            return
        }
        const t = input.text.trim()
        if (root.editing) {
            root.session.commitEdit(t)
            return
        }
        if (t.length > 0) {
            root.session.sendText(t)
        }
        root.session.keepDraft(root.session.activePeer, "")
        root.showText("")
    }

    Component.onCompleted: {
        if (root.session) {
            root.draftPeer = root.session.activePeer
            root.showText(root.session.draftFor(root.draftPeer))
        }
    }
    Component.onDestruction: {
        if (root.session && !root.editing) {
            root.session.keepDraft(root.draftPeer, input.text)
        }
    }

    Connections {
        target: root.session
        function onEditingChanged() {
            if (root.session.editing) {
                root.session.keepDraft(root.session.activePeer, input.text)
                root.showText(root.session.editingText)
                input.forceActiveFocus()
                input.deselect()
            } else {
                root.showText(root.session.draftFor(root.session.activePeer))
            }
        }
        function onReplyingChanged() {
            if (root.session.replying) {
                input.forceActiveFocus()
            }
        }
        function onActivePeerChanged() {
            if (root.session.editing) {
                root.session.cancelEdit()
            } else {
                root.session.keepDraft(root.draftPeer, input.text)
            }
            root.session.stoppedTyping()
            root.draftPeer = root.session.activePeer
            root.showText(root.session.draftFor(root.draftPeer))
            if (root.session.activePeer.length > 0 && !App.mobile) {
                Qt.callLater(function() { input.forceActiveFocus() })
            }
        }
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        spacing: 0

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

        Flow {
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
                // Taking focus from the field would close a phone's keyboard under the finger.
                IconButton {
                    id: attachButton
                    iconName: "attach"
                    iconSize: root.kBarIconSize
                    focusPolicy: Qt.NoFocus
                    visible: !root.editing
                    enabled: !root.switchedOff
                    Layout.preferredWidth: root.kPairedButtonWidth
                    Layout.preferredHeight: root.minInputH
                    onClicked: attachMenu.popup(attachButton, 0,
                        -attachMenu.implicitHeight - root.kAttachMenuGap)
                }
                IconButton {
                    iconName: "smile"
                    iconSize: root.kBarIconSize
                    focusPolicy: Qt.NoFocus
                    enabled: !root.switchedOff
                    Layout.preferredWidth: root.kPairedButtonWidth
                    Layout.preferredHeight: root.minInputH
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
                id: inputFrame
                Layout.fillWidth: true
                Layout.preferredHeight: root.inputH
                color: "transparent"

                // Never takes the focus: a phone keyboard stays open while a name is chosen.
                Popup {
                    id: mentionList
                    visible: root.mentionChoices.length > 0
                    y: -height - root.kAttachMenuGap
                    width: Math.min(inputFrame.width, root.kMentionListWidth)
                    padding: 4
                    modal: false
                    focus: false
                    closePolicy: Popup.NoAutoClose
                    background: Rectangle {
                        color: Theme.surface
                        radius: Theme.radiusSmall
                        border.color: Theme.border
                        border.width: 1
                    }
                    contentItem: Column {
                        Repeater {
                            model: root.mentionChoices
                            delegate: ItemDelegate {
                                id: choice
                                required property string modelData
                                width: mentionList.availableWidth
                                focusPolicy: Qt.NoFocus
                                leftPadding: root.kMentionRowPadding
                                rightPadding: root.kMentionRowPadding
                                topPadding: root.kMentionRowSpacing
                                bottomPadding: root.kMentionRowSpacing
                                implicitHeight: implicitContentHeight + topPadding + bottomPadding
                                contentItem: Label {
                                    text: choice.modelData
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: choice.down || choice.hovered
                                        || (!App.mobile && root.mentionChoices.length === 1)
                                        ? Theme.surfaceAlt : "transparent"
                                }
                                onClicked: root.completeMention(choice.modelData)
                            }
                        }
                    }
                }

                ScrollView {
                    id: inputScroll
                    anchors.fill: parent
                    anchors.leftMargin: 8
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
                        leftPadding: root.kFieldPadding
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
                            if (!root.session || root.restoring) {
                                return
                            }
                            if (input.activeFocus
                                    && (input.length > 0 || input.preeditText.length > 0)) {
                                root.session.typed()
                            } else {
                                root.session.stoppedTyping()
                            }
                        }
                        onTextChanged: input.noteTyping()
                        onPreeditTextChanged: input.noteTyping()
                        onActiveFocusChanged: {
                            if (!input.activeFocus && root.session) {
                                root.session.stoppedTyping()
                            }
                        }
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
                                : (root.editing ? qsTr("Edit message...")
                                    : (root.replying ? qsTr("Reply...") : qsTr("Message...")))
                            color: Theme.textDim
                            font: input.font
                            elide: Text.ElideRight
                        }
                        Keys.onReturnPressed: function(event) {
                            if (App.mobile || (event.modifiers & Qt.ShiftModifier)) {
                                event.accepted = false
                            } else {
                                root.send()
                                event.accepted = true
                            }
                        }
                        Keys.onEnterPressed: function(event) {
                            if (App.mobile || (event.modifiers & Qt.ShiftModifier)) {
                                event.accepted = false
                            } else {
                                root.send()
                                event.accepted = true
                            }
                        }
                        Keys.onTabPressed: function(event) {
                            const choices = root.mentionChoices
                            event.accepted = choices.length > 0
                            if (choices.length === 1) {
                                root.completeMention(choices[0])
                            }
                        }
                        Keys.onPressed: function(event) {
                            const paste = (event.key === Qt.Key_V
                                    && (event.modifiers & Qt.ControlModifier))
                                || event.key === Qt.Key_Paste
                            if (paste && App.clipboardHasImage()) {
                                root.sendClipboardPicture()
                                event.accepted = true
                                return
                            }
                            if (event.modifiers !== Qt.ControlModifier) {
                                return
                            }
                            const form = root.kForms.find((candidate) => candidate.key === event.key)
                            if (form !== undefined) {
                                root.wrapSelection(form.open, form.close)
                                event.accepted = true
                            }
                        }
                    }
                }
            }

            IconButton {
                id: sendButton
                readonly property bool records: !root.editing
                    && input.length === 0 && input.preeditText.length === 0
                iconName: root.editing ? "check" : (records ? "mic" : "send")
                iconSize: root.kBarIconSize
                focusPolicy: Qt.NoFocus
                tint: Theme.green
                enabled: !root.switchedOff
                Layout.alignment: Qt.AlignBottom
                Layout.preferredHeight: root.minInputH
                // The pressed fill would sink the green below 3:1 in the light theme; the growth is the feedback.
                background: Rectangle {
                    radius: 8
                    color: sendButton.hovered && !sendButton.down ? Theme.surfaceAlt : "transparent"
                }
                onPressed: pressGrowth.restart()
                onClicked: records ? voiceSheet.open() : root.send()
                SequentialAnimation {
                    id: pressGrowth
                    NumberAnimation {
                        target: sendButton; property: "scale"
                        to: root.kSendPressGrowth; duration: root.kSendPressMs
                        easing.type: Easing.OutQuad
                    }
                    NumberAnimation {
                        target: sendButton; property: "scale"
                        to: 1; duration: root.kSendPressMs
                        easing.type: Easing.InQuad
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        onAccepted: { sendOptions.fileUrl = selectedFile; sendOptions.open() }
    }

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
            iconSize: Theme.fontEmoji
            text: qsTr("File")
            iconName: "file"
            onTriggered: fileDialog.open()
        }
        ContextMenuItem {
            iconSize: Theme.fontEmoji
            text: qsTr("Image")
            iconName: "image"
            readonly property bool offered: App.canPickPictures
            visible: offered
            height: offered ? implicitHeight : 0
            onTriggered: App.mobile ? App.pickPictures() : imageDialog.open()
        }
        ContextMenuItem {
            iconSize: Theme.fontEmoji
            text: qsTr("Photo")
            iconName: "camera"
            onTriggered: photoSheet.open()
        }
    }

    PhotoCapture {
        id: photoSheet
        onConfirmed: function(shot) { if (root.session) { root.session.sendShot(shot) } }
    }

    VoiceRecorder {
        id: voiceSheet
        session: root.session
        parent: Overlay.overlay
        anchors.centerIn: parent
    }
}
