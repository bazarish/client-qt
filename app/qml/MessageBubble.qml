import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Item {
    id: delegate
    property var session: null
    // Briefly true when a search jump lands on this message, to flash it.
    property bool highlighted: false
    // Asks the view to confirm and delete this message (irreversible; for one's
    // own one-to-one message it is removed at the recipient too).
    signal deleteRequested(var msgId, string protocolId, bool outgoing)
    width: ListView.view ? ListView.view.width : 0
    height: isSystem ? (sysLabel.implicitHeight + 12) : (bubble.height + 4)

    // An attachment card is shown both for an incoming message (which carries a
    // content-store ref) and for one's own outgoing file (which has the type set
    // locally before the upload finishes, so the ref is not there yet).
    readonly property bool isAttachment: (model.attRef && model.attRef.length > 0)
        || (model.outgoing && (model.type === "file" || model.type === "photo"
            || model.type === "audio"))
    readonly property bool isUnsupported: model.type === "unsupported"
    readonly property bool isSystem: model.type === "system"
    // The author of an incoming group message (empty for one-to-one chats).
    readonly property string senderName: (model.sender && model.sender.length > 0 && !model.outgoing && delegate.session)
        ? delegate.session.shortFingerprint(model.sender) : ""

    // Centered system notice (e.g. "added to a group").
    Label {
        id: sysLabel
        visible: delegate.isSystem
        anchors.centerIn: parent
        width: parent.width - 80
        text: model.text
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
        font.italic: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    // The inline keyboard attached to this message (rows of buttons), parsed
    // from its JSON wire form; empty when there is none.
    readonly property var keyboardButtons: {
        if (!model.keyboard || model.keyboard.length === 0) return []
        try { return JSON.parse(model.keyboard) } catch (e) { return [] }
    }
    // The keyboard message's protocol id, sent back as a callback's ref.
    readonly property string msgProtocolId: model.protocolId

    // True for an own text message that can be edited (not an attachment or an
    // unsupported placeholder).
    readonly property bool canEdit: model.outgoing && model.type === "text"
        && !delegate.isAttachment && !delegate.isUnsupported
    // The whole message body for "Copy all" (the file name for an attachment).
    readonly property string fullText: delegate.isAttachment
        ? (model.attName || "") : (model.text || "")

    // Transient "waiting for the bot" state set when a keyboard button is
    // tapped; cleared when the message's content changes (the reply edited it)
    // or after a short fallback timeout, so a tap always visibly registers.
    property bool busy: false
    readonly property string contentKey: (model.text || "") + "" + (model.keyboard || "")
    onContentKeyChanged: delegate.busy = false
    Timer { id: busyTimer; interval: 6000; onTriggered: delegate.busy = false }

    // Single round indicator, coloured by delivery status (see DeliveryStatus).
    function statusColor(s) {
        if (s === DeliveryStatus.AtSenderServer) return Theme.textDim   // grey
        if (s === DeliveryStatus.AtRecipientServer) return Theme.warn    // amber
        if (s === DeliveryStatus.Delivered) return Theme.success        // green
        if (s === DeliveryStatus.Failed) return Theme.danger            // red
        return "transparent"                                            // sending (hollow ring)
    }
    function statusText(s) {
        if (s === DeliveryStatus.AtSenderServer) return "Received by your server"
        if (s === DeliveryStatus.AtRecipientServer) return "Handed to the recipient's server"
        if (s === DeliveryStatus.Delivered) return "Delivered"
        if (s === DeliveryStatus.Failed) return "Failed to send"
        return "Sending…"
    }

    Rectangle {
        id: bubble
        visible: !delegate.isSystem
        anchors.left: model.outgoing ? undefined : parent.left
        anchors.right: model.outgoing ? parent.right : undefined
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        width: Math.min(Math.max(content.implicitWidth + 20, 80), delegate.width * 0.72)
        height: content.implicitHeight + 14
        radius: 12
        color: model.outgoing ? Theme.bubbleOut : Theme.bubbleIn
        // Search-jump flash: a brief accent outline on the targeted message.
        border.width: delegate.highlighted ? 2 : 0
        border.color: Theme.accent
        Behavior on border.width { NumberAnimation { duration: 220 } }

        ColumnLayout {
            id: content
            x: 10
            y: 7
            width: parent.width - 20
            spacing: 4

            // Author of an incoming group message.
            Label {
                visible: delegate.senderName.length > 0
                text: delegate.senderName
                color: Theme.accent
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            // Attachment card.
            ColumnLayout {
                visible: delegate.isAttachment
                spacing: 2
                Layout.fillWidth: true
                Label { text: "📎 " + model.attName; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { visible: model.attSize > 0; text: (model.attSize / 1024).toFixed(1) + " KB"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                // Upload feedback on one's own file while it is actively being
                // sent (status stays Sending only during the live upload; an
                // interrupted send is demoted to Failed on load). Shows the real
                // byte percentage once known, falling back to an indeterminate bar
                // before the first progress callback arrives.
                RowLayout {
                    visible: model.outgoing && model.status === DeliveryStatus.Sending
                    Layout.fillWidth: true
                    spacing: 6
                    ProgressBar {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        from: 0
                        to: 1
                        indeterminate: model.uploadProgress < 0
                        value: model.uploadProgress >= 0 ? model.uploadProgress : 0
                    }
                    Label {
                        text: model.uploadProgress >= 0
                            ? Math.round(model.uploadProgress * 100) + "%"
                            : "Uploading…"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                }
                Button {
                    visible: !model.outgoing
                    text: "Save"
                    // Pick a destination folder; the file is saved under the
                    // message's own name (the native Save dialog will not reliably
                    // pre-fill a suggested name, so the user only chooses where).
                    onClicked: {
                        var dir = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
                        if (("" + dir).length === 0) {
                            dir = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                        }
                        saveDialog.currentFolder = dir
                        saveDialog.open()
                    }
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    contentItem: Label { text: parent.text; color: Theme.accent; horizontalAlignment: Text.AlignHCenter }
                }
            }

            // Unsupported type placeholder (forward compatibility).
            Label {
                visible: delegate.isUnsupported
                text: "Unsupported message (" + model.text + ") — update your app"
                color: Theme.textDim
                font.italic: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            // Plain text. A read-only TextEdit (not a Label) so the user can
            // select text with the mouse and copy it (Ctrl+C); "Copy all" in the
            // context menu copies the whole message.
            TextEdit {
                id: bodyText
                visible: !delegate.isAttachment && !delegate.isUnsupported && model.text.length > 0
                text: model.text
                color: Theme.text
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                selectionColor: Theme.accent
                selectedTextColor: Theme.bg
                Layout.fillWidth: true
            }

            // Inline keyboard (interactive message): rows of tappable buttons.
            // Shown on incoming messages; a tap sends a bot.callback (data) or a
            // bot.command (command) back to the sender.
            ColumnLayout {
                visible: !model.outgoing && delegate.keyboardButtons.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 4
                Repeater {
                    model: delegate.keyboardButtons
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: modelData
                            Button {
                                id: kbButton
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                text: modelData.text
                                hoverEnabled: true
                                enabled: !delegate.busy
                                opacity: delegate.busy ? 0.5 : 1.0
                                onClicked: {
                                    delegate.busy = true
                                    busyTimer.restart()
                                    if (modelData.data !== undefined)
                                        delegate.session.sendCallback(modelData.data, delegate.msgProtocolId)
                                    else if (modelData.command !== undefined)
                                        delegate.session.sendCommand(modelData.command, "")
                                }
                                // Pointing-hand cursor over the button.
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                background: Rectangle {
                                    radius: 8
                                    color: kbButton.down ? Theme.accent
                                        : kbButton.hovered ? Theme.bg : Theme.surface
                                    border.color: kbButton.hovered ? Theme.accent : Theme.border
                                    Behavior on color { ColorAnimation { duration: 90 } }
                                }
                                contentItem: Label {
                                    text: kbButton.text
                                    color: kbButton.down ? Theme.bg : Theme.accent
                                    font.weight: Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            // "Sending..." feedback while waiting for the bot's response to a tap.
            RowLayout {
                visible: delegate.busy
                Layout.topMargin: 2
                spacing: 6
                BusyIndicator { running: delegate.busy; implicitWidth: 16; implicitHeight: 16 }
                Label { text: "sending…"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            }

            // Footer: edited marker + time + outgoing status.
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 4
                Label {
                    visible: model.edited === true
                    text: "edited"
                    color: Theme.textDim
                    font.pixelSize: 10
                    font.italic: true
                }
                Label {
                    id: timeLabel
                    text: model.time ? new Date(model.time).toLocaleTimeString(Qt.locale(), "hh:mm") : ""
                    color: Theme.textDim
                    font.pixelSize: 10
                    // Hovering the time reveals the full date and time.
                    HoverHandler { id: timeHover }
                    ToolTip.visible: timeHover.hovered && model.time > 0
                    ToolTip.text: model.time
                        ? new Date(model.time).toLocaleString(Qt.locale(), "dddd, d MMMM yyyy, hh:mm:ss")
                        : ""
                }
                Rectangle {
                    visible: model.outgoing
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 9
                    implicitHeight: 9
                    radius: width / 2
                    color: delegate.statusColor(model.status)
                    border.width: model.status === DeliveryStatus.Sending ? 1 : 0
                    border.color: Theme.textDim
                    HoverHandler { id: statusHover }
                    ToolTip.visible: statusHover.hovered
                    ToolTip.text: delegate.statusText(model.status)
                }
            }

            // Delivery-failure notice for an outgoing message: the reason and a
            // resend action, shown on the message itself rather than as an
            // application-wide banner.
            RowLayout {
                visible: model.outgoing && model.status === DeliveryStatus.Failed
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 8
                Label {
                    text: (model.error && model.error.length > 0) ? model.error : "Failed to send"
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                // Resend covers one-to-one text and files (group messages go
                // through other send paths and have no resend affordance).
                Label {
                    id: resendLink
                    visible: (!model.sender || model.sender.length === 0)
                        && (model.type === "text" || model.type === "file"
                            || model.type === "photo" || model.type === "audio")
                    text: "Resend"
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (model.type === "text") {
                                delegate.session.resendText(
                                    model.msgId, model.text, model.protocolId)
                            } else {
                                delegate.session.resendFile(model.msgId, model.protocolId)
                            }
                        }
                    }
                }
            }
        }

        // Right-click or long-press a message to open its context menu (copy /
        // edit own text / delete).
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: contextMenu.popup()
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onLongPressed: contextMenu.popup()
        }
        Menu {
            id: contextMenu
            MenuItem {
                text: "Copy all"
                enabled: delegate.fullText.length > 0
                onTriggered: delegate.session.copyText(delegate.fullText)
            }
            MenuItem {
                text: "Edit"
                visible: delegate.canEdit
                height: visible ? implicitHeight : 0
                onTriggered: delegate.session.beginEdit(model.msgId, model.protocolId, model.text)
            }
            MenuItem {
                text: "Delete"
                onTriggered: delegate.deleteRequested(model.msgId, model.protocolId, model.outgoing)
            }
        }
    }

    // A directory picker: the attachment is saved into the chosen folder under
    // the message's original file name (joined in C++ by saveAttachmentToFolder),
    // so the user only chooses where. The title shows the name being saved.
    FolderDialog {
        id: saveDialog
        title: "Save \"" + (model.attName || "file") + "\" to folder"
        onAccepted: delegate.session.saveAttachmentToFolder(
            model.attRef, model.attKey, selectedFolder, model.attName)
    }
}
