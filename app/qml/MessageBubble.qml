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
    // Asks the view to open the emoji picker / the reactions-and-views modal for
    // this message (handled by a single shared popup, not one per bubble).
    signal reactRequested(string protocolId)
    signal reactionDetailsRequested(string protocolId)
    width: ListView.view ? ListView.view.width : 0
    height: isSystem ? (sysLabel.implicitHeight + 12) : (bubble.height + 4)

    // Human-readable byte count for the download progress line.
    function humanSize(n) {
        if (!n || n <= 0) {
            return "0 B"
        }
        const u = ["B", "KB", "MB", "GB"]
        var v = n
        var i = 0
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return (i === 0 ? v : v.toFixed(1)) + " " + u[i]
    }

    // An attachment card is shown both for an incoming message (which carries a
    // content-store ref) and for one's own outgoing file (which has the type set
    // locally before the upload finishes, so the ref is not there yet).
    readonly property bool isAttachment: (model.attRef && model.attRef.length > 0)
        || (model.outgoing && (model.type === "file" || model.type === "photo"
            || model.type === "audio"))
    readonly property bool isUnsupported: model.type === "unsupported"
    readonly property bool isSystem: model.type === "system"
    // A "group photo set" notice: rendered as a normal bubble from the author, but
    // carrying a service line plus the new group photo and a thin white outline.
    readonly property bool isGroupAvatar: model.type === "group.avatar"
    // A "group renamed" notice: a bubble from the author with a thin white outline.
    readonly property bool isGroupRename: model.type === "group.rename"
    // Group service notices share the thin white outline.
    readonly property bool isGroupService: isGroupAvatar || isGroupRename
    // A contact request. Incoming ones render green with an "Agree" button; our own
    // outgoing one is a "request sent" note.
    readonly property bool isContactRequest: model.type === "contact.request"
    readonly property bool isContactRequestIncoming: isContactRequest && !model.outgoing
    // The author of an incoming group message: { name, isContact, fpShort }. A
    // local contact name reads as trusted (bright); otherwise the sender's own
    // account name (green) with the short fingerprint beneath it. Null in 1:1 chats.
    readonly property var senderInfo: {
        if (!(model.sender && model.sender.length > 0 && !model.outgoing && delegate.session)) {
            return null
        }
        // Read the member list so this re-resolves when the active group's roster
        // and member self-names arrive (they land asynchronously after open).
        void delegate.session.activeGroupMembers
        return delegate.session.groupSenderInfo(model.sender)
    }
    readonly property bool hasSender: senderInfo !== null

    // The message this one replies to, resolved against local history:
    // { found, localId, text, sender }. Null when this is not a reply.
    readonly property var replyInfo: (model.replyTo && model.replyTo.length > 0 && delegate.session)
        ? delegate.session.replyPreview(model.replyTo) : null
    // The reply quote is shown ONLY when the original is in local history; an
    // unresolved reference shows no quote at all. Declared on the delegate (not on
    // `content`) so the quote Rectangle's visible/height bindings resolve it.
    readonly property bool hasReplyQuote: delegate.replyInfo !== null && delegate.replyInfo.found

    // Whether this is a group chat (the "who reacted / viewed" detail makes sense
    // only there; a 1:1 chat has a single peer).
    readonly property bool inGroup: delegate.session
        && delegate.session.isGroup(delegate.session.activePeer)
    // A real message can carry reactions (not a service notice, request or
    // placeholder, and it must have a protocol id to reference).
    readonly property bool reactable: !delegate.isSystem && !delegate.isUnsupported
        && !delegate.isGroupService && !delegate.isContactRequest
        && model.protocolId && model.protocolId.length > 0
    // The reaction chips for this message: [{ emoji, count, mine }], re-queried
    // whenever any reaction changes (reactionsRevision drives the binding).
    readonly property var reactions: (delegate.session && delegate.reactable
        && delegate.session.reactionsRevision >= 0)
        ? delegate.session.reactionSummary(model.protocolId) : []

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
        // Search-jump flash: a brief accent outline on the targeted message. A
        // group service notice keeps a permanent thin white outline, and an
        // incoming contact request a green one, so they stand apart.
        border.width: delegate.highlighted ? 2
            : ((delegate.isGroupService || delegate.isContactRequestIncoming) ? 1 : 0)
        border.color: delegate.isContactRequestIncoming ? Theme.green : Theme.accent
        Behavior on border.width { NumberAnimation { duration: 220 } }

        ColumnLayout {
            id: content
            x: 10
            y: 7
            width: parent.width - 20
            spacing: 4

            // Author of an incoming group message: a name (a local contact name
            // reads bright/trusted; a sender's own account name reads green) and,
            // for the latter, the short fingerprint beneath it as the ground truth.
            ColumnLayout {
                visible: delegate.hasSender
                spacing: 0
                Layout.fillWidth: true
                Label {
                    text: delegate.senderInfo ? delegate.senderInfo.name : ""
                    color: (delegate.senderInfo && delegate.senderInfo.isContact) ? Theme.accent : Theme.green
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: delegate.senderInfo && delegate.senderInfo.fpShort.length > 0
                    text: delegate.senderInfo ? "(" + delegate.senderInfo.fpShort + ")" : ""
                    color: Theme.textDim
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            // Reply quote: the message this one replies to. Shown ONLY when the
            // original is in local history (then it is a clickable jump to it); a
            // reference we cannot resolve shows no quote at all.
            Rectangle {
                id: replyQuote
                visible: delegate.hasReplyQuote
                Layout.fillWidth: true
                Layout.preferredHeight: delegate.hasReplyQuote ? replyCol.implicitHeight + 8 : 0
                radius: 6
                color: Theme.surface
                border.color: Theme.border
                border.width: 1
                // A neon accent bar on the leading edge, like a quote rule.
                Rectangle { width: 3; height: parent.height - 8; y: 4; x: 0; radius: 1; color: Theme.accent }
                ColumnLayout {
                    id: replyCol
                    x: 10
                    y: 4
                    width: parent.width - 16
                    spacing: 0
                    Label {
                        text: delegate.replyInfo ? delegate.replyInfo.sender : ""
                        color: Theme.accent
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: delegate.replyInfo ? delegate.replyInfo.text : ""
                        color: Theme.textDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        Layout.fillWidth: true
                    }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: if (delegate.session) {
                        delegate.session.openConversationAtMessage(
                            delegate.session.activePeer, delegate.replyInfo.localId)
                    }
                }
            }

            // Contact request: an incoming one reads green with an "Agree" button
            // (no decline - ignoring it is the decline); our own outgoing one is a
            // "request sent" note.
            ColumnLayout {
                visible: delegate.isContactRequest
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: delegate.isContactRequestIncoming
                        ? "wants to add you as a contact"
                        : "Contact request sent"
                    color: Theme.green
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: model.text.length > 0
                    text: model.text
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Button {
                    id: agreeButton
                    // Re-evaluated on contact changes via contactsRevision.
                    visible: delegate.isContactRequestIncoming && delegate.session
                        && delegate.session.contactsRevision >= 0
                        && delegate.session.contactCanAccept(delegate.session.activePeer)
                    text: "Agree"
                    hoverEnabled: true
                    onClicked: if (delegate.session) { delegate.session.acceptContact() }
                    background: Rectangle {
                        radius: 8
                        color: agreeButton.down ? Qt.darker(Theme.green, 1.2)
                            : (agreeButton.hovered ? Qt.darker(Theme.green, 1.1) : Theme.green)
                    }
                    contentItem: Label {
                        text: agreeButton.text
                        color: Theme.bg
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        leftPadding: 16
                        rightPadding: 16
                    }
                }
            }

            // Group rename notice: a service line in a white-outlined bubble.
            Label {
                visible: delegate.isGroupRename
                text: model.text  // "changed the group name to ..."
                color: Theme.textDim
                font.italic: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
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
                // Download progress for an incoming attachment being saved: a real
                // bytes received / total bar with a percentage (the I2P stream is
                // read in chunks). Indeterminate only briefly, before the first
                // byte arrives.
                ColumnLayout {
                    id: dlProgress
                    visible: model.downloading
                    Layout.fillWidth: true
                    spacing: 2
                    // downloadStage: 0 connecting, 1 downloading, 2 reconnecting.
                    readonly property bool reconnecting: model.downloadStage === 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        ProgressBar {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 4
                            from: 0
                            to: 1
                            // Hold the bar at the bytes we have while reconnecting,
                            // rather than dropping back to an indeterminate sweep.
                            indeterminate: model.downloadTotal <= 0 && !dlProgress.reconnecting
                            value: model.downloadTotal > 0
                                ? model.downloadReceived / model.downloadTotal : 0
                        }
                        Label {
                            text: model.downloadTotal > 0
                                ? Math.round(model.downloadReceived / model.downloadTotal * 100) + "%"
                                : ""
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: dlProgress.reconnecting
                            ? (model.downloadTotal > 0
                                ? "Reconnecting… " + delegate.humanSize(model.downloadReceived)
                                    + " / " + delegate.humanSize(model.downloadTotal)
                                : "Reconnecting over I2P…")
                            : (model.downloadTotal > 0
                                ? (delegate.humanSize(model.downloadReceived) + " / "
                                    + delegate.humanSize(model.downloadTotal))
                                : "Connecting over I2P…")
                        color: dlProgress.reconnecting ? Theme.warn : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                }
                // A failed save: the reason, inline. The Save button reappears so
                // the user can retry.
                Label {
                    visible: model.downloadError.length > 0
                    Layout.fillWidth: true
                    text: "Save failed: " + model.downloadError
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                // The blob aged out of the store (404/410): a permanent,
                // non-retryable state (persisted across restarts), so the Save
                // button is dropped and this stands in its place.
                Label {
                    visible: model.blobGone
                    Layout.fillWidth: true
                    text: "Not found"
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                }
                Button {
                    id: saveButton
                    visible: !model.outgoing && !model.downloading && !model.blobGone
                    // Once saved and the file is still on disk, offer to open it;
                    // otherwise (never saved, or the file is gone) offer Save.
                    readonly property bool savedExists: model.savedPath.length > 0
                        && delegate.session && delegate.session.fileExists(model.savedPath)
                    text: savedExists ? "Open" : "Save"
                    onClicked: {
                        // Re-check on click so a file deleted since the last load
                        // falls back to re-saving rather than revealing a stale path.
                        if (model.savedPath.length > 0 && delegate.session
                                && delegate.session.fileExists(model.savedPath)) {
                            delegate.session.showInFolder(model.savedPath)
                            return
                        }
                        // Snapshot the attachment onto the shared dialog and seed the
                        // native picker with the message's file name in Downloads.
                        // currentFile (not selectedFile) is what pre-fills the
                        // suggested name in SaveFile mode here - matching the export
                        // backup dialog, which is the pattern that actually pre-fills.
                        saveDialog.attRef = model.attRef
                        saveDialog.attKey = model.attKey
                        saveDialog.token = model.msgId
                        saveDialog.currentFile = delegate.session.defaultSaveUrl(model.attName)
                        saveDialog.open()
                    }
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    contentItem: Label { text: saveButton.text; color: Theme.accent; horizontalAlignment: Text.AlignHCenter }
                }
            }

            // Group photo set notice: a service line plus the new group photo
            // (the group's current avatar, keyed by the group id = the active peer).
            ColumnLayout {
                visible: delegate.isGroupAvatar
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: model.text  // "set the group photo"
                    color: Theme.textDim
                    font.italic: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Avatar {
                    Layout.alignment: Qt.AlignHCenter
                    fingerprint: delegate.session ? delegate.session.activePeer : ""
                    size: 140
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
                visible: !delegate.isAttachment && !delegate.isUnsupported && !delegate.isGroupAvatar
                    && !delegate.isGroupRename && !delegate.isContactRequest && model.text.length > 0
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

            // Reaction chips: one per distinct emoji with its count; the one we set
            // is outlined. Tapping a chip toggles our reaction to that emoji.
            Flow {
                visible: delegate.reactions.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 4
                Repeater {
                    model: delegate.reactions
                    Rectangle {
                        required property var modelData
                        height: 22
                        width: chipRow.implicitWidth + 12
                        radius: 11
                        color: modelData.mine ? Qt.rgba(0.22, 0.5, 0.2, 0.35) : Theme.surface
                        border.width: 1
                        border.color: modelData.mine ? Theme.green : Theme.border
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 3
                            Label {
                                text: modelData.emoji
                                font.pixelSize: 13
                                // Colour emoji need the bundled emoji font + the
                                // native renderer (the default is monochrome).
                                font.family: Theme.emojiFontFamily
                                renderType: Text.NativeRendering
                            }
                            Label {
                                visible: modelData.count > 1
                                text: modelData.count
                                color: Theme.textDim
                                font.pixelSize: 11
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: if (delegate.session) {
                                delegate.session.react(delegate.msgProtocolId, modelData.emoji)
                            }
                            // Long-press / right-click a chip opens the who-reacted
                            // detail in a group chat.
                            onLongPressed: if (delegate.inGroup) {
                                delegate.reactionDetailsRequested(delegate.msgProtocolId)
                            }
                        }
                    }
                }
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
                text: "Reply"
                // Any real message (text or attachment) can be replied to; service
                // notices, requests and unsupported placeholders cannot.
                visible: !delegate.isSystem && !delegate.isUnsupported
                    && !delegate.isGroupService && !delegate.isContactRequest
                    && model.protocolId && model.protocolId.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: {
                    var preview = delegate.isAttachment ? (model.attName || "") : (model.text || "")
                    var who = model.outgoing ? "You"
                        : (delegate.senderInfo ? delegate.senderInfo.name
                            : (delegate.session ? delegate.session.activePeerName : ""))
                    delegate.session.beginReply(model.protocolId, preview, who)
                }
            }
            MenuItem {
                text: "React…"
                visible: delegate.reactable
                height: visible ? implicitHeight : 0
                onTriggered: delegate.reactRequested(model.protocolId)
            }
            MenuItem {
                // Group only: who reacted, and (for our own messages) who has read it.
                text: "Reactions & views"
                visible: delegate.reactable && delegate.inGroup
                height: visible ? implicitHeight : 0
                onTriggered: delegate.reactionDetailsRequested(model.protocolId)
            }
            MenuItem {
                text: "Copy all"
                // Only for text messages: an attachment, a service notice or a
                // request has nothing to copy.
                visible: !delegate.isAttachment && !delegate.isGroupService
                    && !delegate.isContactRequest
                height: visible ? implicitHeight : 0
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

    // Native Save dialog: the OS file picker pre-filled with the message's file
    // name, so it resolves any name conflict itself. On accept the download runs in
    // the background with its byte progress shown on this bubble. attRef/attKey/token
    // are snapshotted on open so a recycled delegate cannot misroute the result.
    FileDialog {
        id: saveDialog
        property string attRef: ""
        property string attKey: ""
        property var token: 0
        title: "Save file"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (delegate.session) {
                delegate.session.saveAttachmentToFile(
                    saveDialog.attRef, saveDialog.attKey, "" + saveDialog.selectedFile,
                    saveDialog.token)
            }
        }
    }
}
