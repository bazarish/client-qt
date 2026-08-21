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
    // Asks the view to open the emoji picker for this message (handled by a single
    // shared popup, not one per bubble).
    signal imageRequested(url source, string messageId, string name)
    signal reactRequested(string protocolId)
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
    // A picture is its own kind of message: its bytes are in the profile, it is
    // drawn where it stands, and it never becomes a file card.
    readonly property bool isPicture: model.type === "image"
    readonly property bool isVoice: model.type === "voice"
    readonly property bool voicePlaying: delegate.isVoice && delegate.session
        && delegate.session.voicePlaying === model.protocolId
    // The picture as this profile holds it. The revision in the URL is what makes
    // an Image reload when the bytes arrive.
    readonly property string pictureUrl: (delegate.isPicture && model.hasPicture)
        ? "image://picture/" + model.protocolId + "?r=" + Pictures.revision : ""
    // The profile holds no drawable picture for this message: the bytes came with
    // it, so nothing is on its way and there is nothing to ask for. No fallback,
    // no Save button - it says it is broken.
    readonly property bool pictureBroken: delegate.isPicture && !model.hasPicture
    readonly property bool isAttachment: !delegate.isPicture && !delegate.isVoice
        && ((model.attName && model.attName.length > 0)
            || (model.outgoing && (model.type === "file" || model.type === "audio")))
    readonly property bool isUnsupported: model.type === "unsupported"
    readonly property bool isSystem: model.type === "system"
    // A contact request. Incoming ones render green with an "Agree" button; our own
    // outgoing one is a "request sent" note.
    readonly property bool isContactRequest: model.type === "contact.request"
    readonly property bool isContactRequestIncoming: isContactRequest && !model.outgoing

    // The message this one replies to, resolved against local history:
    // { found, localId, text, sender }. Null when this is not a reply.
    readonly property var replyInfo: (model.replyTo && model.replyTo.length > 0 && delegate.session)
        ? delegate.session.replyPreview(model.replyTo) : null
    // The reply quote is shown ONLY when the original is in local history; an
    // unresolved reference shows no quote at all. Declared on the delegate (not on
    // `content`) so the quote Rectangle's visible/height bindings resolve it.
    readonly property bool hasReplyQuote: delegate.replyInfo !== null && delegate.replyInfo.found

    // A real message can carry reactions (not a service notice, request or
    // placeholder, and it must have a protocol id to reference).
    readonly property bool reactable: !delegate.isSystem && !delegate.isUnsupported
        && !delegate.isContactRequest
        && model.protocolId && model.protocolId.length > 0
    // The reaction chips for this message: [{ emoji, count, mine }], re-queried
    // whenever any reaction changes (reactionsRevision drives the binding).
    readonly property var reactions: (delegate.session && delegate.reactable
        && delegate.session.reactionsRevision >= 0)
        ? delegate.session.reactionSummary(model.protocolId) : []

    // Centered system notice (e.g. a cleared-chat note).
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
        // Search-jump flash: a brief accent outline on the targeted message. An
        // incoming contact request keeps a permanent thin green outline, so it
        // stands apart.
        border.width: delegate.highlighted ? 2
            : (delegate.isContactRequestIncoming ? 1 : 0)
        border.color: delegate.isContactRequestIncoming ? Theme.green : Theme.accent
        Behavior on border.width { NumberAnimation { duration: 220 } }

        ColumnLayout {
            id: content
            x: 10
            y: 7
            width: parent.width - 20
            spacing: 4

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
                    readonly property bool inFlight: delegate.session
                        && delegate.session.acceptingContact === delegate.session.activePeer
                    text: inFlight ? "Agreeing…" : "Agree"
                    enabled: !inFlight
                    hoverEnabled: true
                    onClicked: if (delegate.session) { delegate.session.acceptContact() }
                    background: Rectangle {
                        radius: 8
                        color: !agreeButton.enabled ? Theme.surfaceAlt
                            : (agreeButton.down ? Qt.darker(Theme.green, 1.2)
                            : (agreeButton.hovered ? Qt.darker(Theme.green, 1.1) : Theme.green))
                    }
                    contentItem: Label {
                        text: agreeButton.text
                        color: agreeButton.enabled ? Theme.bg : Theme.textDim
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        leftPadding: 16
                        rightPadding: 16
                    }
                }
            }

            // Attachment card. Built only for a message that carries a file: it
            // is the heaviest thing in this delegate, and a chat is mostly text.
            Loader {
                active: delegate.isAttachment
                visible: active
                Layout.fillWidth: true
                sourceComponent: ColumnLayout {
                    spacing: 2
                    Label { text: "" + model.attName; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { visible: model.attSize > 0; text: delegate.humanSize(model.attSize); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    // Upload feedback on one's own file while it is actively being
                    // sent (status stays Sending only during the live upload; an
                    // interrupted send is demoted to Failed on load). Shows the real
                    // byte percentage once known, falling back to an indeterminate bar
                    // before the first progress callback arrives.
                    // The stages differ in length, so the block is sized once for the
                    // longest of them: a bubble that resizes on every step is unreadable.
                    TextMetrics {
                        id: stageMetrics
                        font.pixelSize: Theme.fontSmall
                        text: "Publishing the address"
                    }

                    ColumnLayout {
                        // A file is served on demand, so this block also carries the
                        // steps before any byte moves: the request arriving, the
                        // one-time address being built and published.
                        // Only while bytes are actually moving. The message that
                        // announces a transfer is a small message like any other,
                        // and a progress bar on it says something untrue.
                        visible: model.outgoing && model.transferStage.length > 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: stageMetrics.width
                        spacing: 2
                        RowLayout {
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
                                    : ""
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: model.transferStage.length > 0 ? model.transferStage : "Sending…"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                    // Download progress for an incoming attachment being saved: a real
                    // bytes received / total bar with a percentage (the I2P stream is
                    // read in chunks). Indeterminate only briefly, before the first
                    // byte arrives.
                    ColumnLayout {
                        id: dlProgress
                        // Incoming only: an outgoing file has its own block above, and
                        // showing both put two bars in the sender's bubble.
                        visible: !model.outgoing
                            && (model.downloading || model.transferStage.length > 0)
                        Layout.fillWidth: true
                        Layout.minimumWidth: stageMetrics.width
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
                                    : (model.transferStage.length > 0
                                        ? model.transferStage
                                        : "Connecting over I2P…"))
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
                            saveDialog.peer = delegate.session.activePeer
                            saveDialog.messageId = model.protocolId
                            saveDialog.token = model.msgId
                            saveDialog.currentFile = delegate.session.defaultSaveUrl(model.attName)
                            saveDialog.open()
                        }
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                        contentItem: Label { text: saveButton.text; color: Theme.accent; horizontalAlignment: Text.AlignHCenter }
                    }
                }
            }

            // A picture: the image itself and its size. Between arriving and
            // being decoded it holds its place rather than collapsing the bubble
            // to nothing.
            ColumnLayout {
                visible: delegate.isPicture
                Layout.fillWidth: true
                spacing: 2

                Rectangle {
                    visible: model.hasPicture && preview.status !== Image.Ready
                    Layout.preferredWidth: preview.maxEdge
                    Layout.preferredHeight: Math.round(preview.maxEdge * 0.6)
                    radius: Theme.radiusSmall
                    color: Theme.deep
                    border.color: Theme.border
                }

                Image {
                    id: preview
                    visible: model.hasPicture && status === Image.Ready
                    source: delegate.pictureUrl
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    // Big enough to see, small enough to keep the chat a chat.
                    readonly property int maxEdge: 320
                    Layout.preferredWidth: Math.min(maxEdge,
                        implicitWidth > 0 ? implicitWidth : maxEdge)
                    Layout.preferredHeight: implicitWidth > 0
                        ? Layout.preferredWidth * (implicitHeight / implicitWidth) : 0
                    sourceSize.width: maxEdge * 2
                    TapHandler {
                        onTapped: delegate.imageRequested(delegate.pictureUrl,
                            model.protocolId, model.attName)
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Label {
                    visible: delegate.pictureBroken
                    text: "Broken picture"
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                }

                Label {
                    visible: model.attSize > 0
                    text: delegate.humanSize(model.attSize)
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                }
            }

            // A voice message: the shape of what was said, how long it runs and
            // what it weighs. The audio is in the profile, decoded straight into
            // the speaker; the waveform was drawn from that same audio when the
            // message was stored.
            RowLayout {
                visible: delegate.isVoice
                Layout.fillWidth: true
                spacing: 8

                IconButton {
                    iconName: delegate.voicePlaying ? "close" : "send"
                    tint: Theme.accent
                    onClicked: delegate.session.playVoice(model.protocolId)
                }

                // One bar per slice of the recording, read out of the stored hex
                // profile. A message stored without one draws nothing rather
                // than an invented shape.
                Row {
                    id: wave
                    readonly property string hex: model.attWave || ""
                    readonly property int kBars: 32
                    readonly property int kMaxHeight: 26
                    visible: hex.length > 0
                    Layout.preferredWidth: kBars * 3 - 1
                    Layout.preferredHeight: kMaxHeight
                    spacing: 1
                    Repeater {
                        model: wave.kBars
                        delegate: Rectangle {
                            required property int index
                            readonly property real level: {
                                const from = Math.floor(index * wave.hex.length / wave.kBars)
                                return parseInt(wave.hex.charAt(from), 16) / 15
                            }
                            width: 2
                            height: Math.max(2, level * wave.kMaxHeight)
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 1
                            color: delegate.voicePlaying ? Theme.accent : Theme.textDim
                        }
                    }
                }

                Label {
                    text: {
                        const total = Math.floor((model.attDurationMs || 0) / 1000)
                        const seconds = total % 60
                        return Math.floor(total / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
                    }
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                }
                Label {
                    text: delegate.humanSize(model.attSize)
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    Layout.fillWidth: true
                }
                // Playback speed, stepped through by pressing it. It belongs to
                // the session, so the choice holds for the next one too.
                Label {
                    text: (delegate.session ? delegate.session.voiceSpeed : 1) + "x"
                    color: (delegate.session && delegate.session.voiceSpeed > 1)
                        ? Theme.accent : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    TapHandler { onTapped: delegate.session.cycleVoiceSpeed() }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
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
                visible: !delegate.isAttachment && !delegate.isUnsupported
                    && !delegate.isContactRequest && model.text.length > 0
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
            // bot.command (command) back to the sender. Built only when there are
            // buttons - almost no message has any.
            Loader {
                active: !model.outgoing && delegate.keyboardButtons.length > 0
                visible: active
                Layout.fillWidth: true
                Layout.topMargin: 2
                sourceComponent: ColumnLayout {
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
                                // A reaction can be any glyph, and a plain
                                // character renders as text: without a colour it
                                // came out black on a dark bubble.
                                color: Theme.text
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
                // Resend covers text and files.
                Label {
                    id: resendLink
                    visible: model.type === "text" || model.type === "file"
                        || model.type === "photo" || model.type === "audio"
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
        FileDialog {
            id: pictureSaveDialog
            property string messageId
            fileMode: FileDialog.SaveFile
            onAccepted: delegate.session.savePictureAs(pictureSaveDialog.messageId, selectedFile)
        }
        ContextMenu {
            id: contextMenu
            ContextMenuItem {
                // Plain ASCII, no "…" / "&": the default MenuItem renderer draws the
                // ellipsis as a tofu box and treats "&" as a mnemonic, which mangled
                // these labels into "React_" / "React_ view".
                text: "React"
                visible: delegate.reactable
                height: visible ? implicitHeight : 0
                onTriggered: delegate.reactRequested(model.protocolId)
            }
            ContextMenuItem {
                // From the profile straight to the clipboard: no file in between.
                text: "Copy picture"
                visible: delegate.pictureUrl.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: delegate.session.copyPicture(model.protocolId)
            }
            ContextMenuItem {
                // A picture lives in the profile database; this is how it leaves
                // it as a file.
                text: "Save as"
                visible: delegate.pictureUrl.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: {
                    pictureSaveDialog.currentFile = delegate.session.defaultPictureSaveUrl(
                        model.protocolId, model.attName)
                    pictureSaveDialog.messageId = model.protocolId
                    pictureSaveDialog.open()
                }
            }
            ContextMenuItem {
                text: "Reply"
                // Any real message (text or attachment) can be replied to; service
                // notices, requests and unsupported placeholders cannot.
                visible: !delegate.isSystem && !delegate.isUnsupported
                    && !delegate.isContactRequest
                    && model.protocolId && model.protocolId.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: {
                    var preview = delegate.isAttachment ? (model.attName || "") : (model.text || "")
                    var who = model.outgoing ? "You"
                        : (delegate.session ? delegate.session.activePeerName : "")
                    delegate.session.beginReply(model.protocolId, preview, who)
                }
            }
            ContextMenuItem {
                text: "Copy all"
                // Only where there is text to copy: an attachment, a request, a
                // picture and a voice message carry none.
                visible: !delegate.isAttachment && !delegate.isContactRequest
                    && delegate.fullText.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: delegate.session.copyText(delegate.fullText)
            }
            ContextMenuItem {
                text: "Edit"
                visible: delegate.canEdit
                height: visible ? implicitHeight : 0
                onTriggered: delegate.session.beginEdit(model.msgId, model.protocolId, model.text)
            }
            ContextMenuItem {
                text: "Delete"
                onTriggered: delegate.deleteRequested(model.msgId, model.protocolId, model.outgoing)
            }
        }
    }

    // Native Save dialog: the OS file picker pre-filled with the message's file
    // name, so it resolves any name conflict itself. On accept the download runs in
    // the background with its byte progress shown on this bubble. peer/messageId/token
    // are snapshotted on open so a recycled delegate cannot misroute the result.
    FileDialog {
        id: saveDialog
        property string peer: ""
        property string messageId: ""
        property var token: 0
        title: "Save file"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (delegate.session) {
                delegate.session.saveAttachmentToFile(
                    saveDialog.peer, saveDialog.messageId, "" + saveDialog.selectedFile,
                    saveDialog.token)
            }
        }
    }
}
