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
    signal deleteRequested(var msgId, string e2eId, bool outgoing)
    // Asks the view to open the emoji picker for this message (handled by a single
    // shared popup, not one per bubble).
    signal imageRequested(url source, string e2eId, string name)
    // Carries where the message is on screen, so the picker opens beside it.
    signal reactRequested(string e2eId, real sceneX, real sceneY)
    // Pass this message on to another chat: the view asks which one.
    signal forwardRequested(string e2eId)
    // A username written in the body was tapped: the view offers the add-a-contact
    // form with the name filled in. Nothing is sent.
    signal aliasRequested(string alias)
    // A web address in the body was tapped: the view warns before anything leaves.
    signal linkRequested(string url)
    // Where the context menu was opened, in scene coordinates: what the window
    // that opens from it anchors to. Taken from the event rather than mapped from
    // this delegate - the handlers sit on the bubble, which is pushed to the right
    // for one's own messages, and mapping through the delegate dropped exactly
    // that offset.
    property point menuAt: Qt.point(0, 0)
    width: ListView.view ? ListView.view.width : 0
    height: isSystem ? (sysLabel.implicitHeight + 12) : (bubble.height + 4)

    // How the body's clickable parts name themselves. The document is built from
    // the message here, so these are the only schemes a tap can carry; a
    // correspondent's own text is escaped and never becomes a reference.
    readonly property string kSendScheme: "bz-send:"
    readonly property string kAliasScheme: "bz-alias:"

    // What a tap inside the body means: send back what the message offered, offer
    // a name to the add-a-contact form, or ask the view about a web address.
    function activateLink(link) {
        if (link.startsWith(delegate.kSendScheme)) {
            if (delegate.session) {
                delegate.session.sendOffered(
                    decodeURIComponent(link.substring(delegate.kSendScheme.length)))
            }
        } else if (link.startsWith(delegate.kAliasScheme)) {
            delegate.aliasRequested(decodeURIComponent(link.substring(delegate.kAliasScheme.length)))
        } else {
            delegate.linkRequested(link)
        }
    }

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
    // A picture is its own kind of message: its bytes are in the account, it is
    // drawn where it stands, and it never becomes a file card.
    readonly property bool isPicture: model.type === "image"
    readonly property bool isVoice: model.type === "voice"
    readonly property bool voicePlaying: delegate.isVoice && delegate.session
        && delegate.session.voicePlaying === model.e2eId
    // The picture as this account holds it. The revision in the URL is what makes
    // an Image reload when the bytes arrive.
    readonly property string pictureUrl: (delegate.isPicture && model.hasPicture)
        ? "image://picture/" + model.e2eId + "?r=" + Pictures.revision : ""
    // The account holds no drawable picture for this message: the bytes came with
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
        && model.e2eId && model.e2eId.length > 0
    // The reaction chips for this message: [{ emoji, count, mine }], re-queried
    // whenever any reaction changes (reactionsRevision drives the binding).
    // Whether a reaction is ordinary printable text rather than an emoji: the
    // bundled emoji font carries no glyphs for digits, so text drawn through it
    // comes out blank. A chip holds one or the other, never both.
    function plainText(glyph) {
        return /^[\x20-\x7E]+$/.test(glyph)
    }
    readonly property var reactions: (delegate.session && delegate.reactable
        && delegate.session.reactionsRevision >= 0)
        ? delegate.session.reactionSummary(model.e2eId) : []
    // The emoji on this message that arrived while nobody was looking at them.
    // They flash once, so that somebody who came here from a notification is
    // shown what it was about instead of having to find it.
    readonly property var freshReactions: (delegate.session && delegate.reactable
        && delegate.session.reactionsRevision >= 0)
        ? delegate.session.reactionsToFlash(model.e2eId) : []

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
    function parseKeyboard(wire) {
        if (!wire || wire.length === 0) {
            return []
        }
        try { return JSON.parse(wire) } catch (e) { return [] }
    }
    readonly property var keyboardButtons: delegate.parseKeyboard(model.keyboard)
    // The keyboard message's protocol id, sent back as a callback's ref.
    readonly property string msgE2eId: model.e2eId

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
        if (s === DeliveryStatus.Delivering) return Theme.textDim       // grey
        if (s === DeliveryStatus.AtRecipientServer) return Theme.warn    // amber
        if (s === DeliveryStatus.Delivered) return Theme.success        // green
        if (s === DeliveryStatus.Failed) return Theme.danger            // red
        return "transparent"                                            // preparing (hollow ring)
    }
    function statusText(s) {
        if (s === DeliveryStatus.Delivering) return "Sending to their server…"
        if (s === DeliveryStatus.AtRecipientServer) return "Handed to the recipient's server"
        if (s === DeliveryStatus.Delivered) return "Delivered"
        if (s === DeliveryStatus.Failed) return "Failed to send"
        return "Preparing an address to send from…"
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

            // Passed on, not written here - said before the content rather than
            // after it, because it is what the content should be read in the
            // light of. The mark is all it is: it names nobody and says nothing
            // about who wrote what follows.
            RowLayout {
                visible: model.forwarded === true
                Layout.fillWidth: true
                Layout.bottomMargin: 2
                spacing: 5
                Icon { name: "forwarded"; color: Theme.textDim; size: 13 }
                Label {
                    text: "Forwarded"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
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
                        // One line of what was said, not a rendering of it.
                        text: delegate.replyInfo ? App.markupPlain(delegate.replyInfo.text) : ""
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
                    // Written by someone who is not a contact yet: their words are
                    // shown, their markup is not. Nothing here is tappable.
                    visible: model.text.length > 0
                    text: App.markupPlain(model.text)
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
                    // The press, and the account's own answer to it: the button
                    // says "Agreeing" until the acceptance is confirmed stored by
                    // their server, and reads as pressable again if it never was.
                    readonly property bool inFlight: delegate.session
                        && delegate.session.contactsRevision >= 0
                        && (delegate.session.acceptingContact === delegate.session.activePeer
                            || delegate.session.contactAgreeing(delegate.session.activePeer))
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
                            // Stop, beside the bar it belongs to: a transfer the
                            // user has changed their mind about ends here as well
                            // as in the activity panel, and this is where they are
                            // looking while it runs.
                            Button {
                                id: stopUpload
                                implicitWidth: 18
                                implicitHeight: 18
                                padding: 0
                                hoverEnabled: true
                                ToolTip.visible: hovered
                                ToolTip.text: "Stop"
                                onClicked: if (delegate.session) {
                                    delegate.session.cancelTransfer(model.e2eId)
                                }
                                background: Rectangle {
                                    radius: 4
                                    color: stopUpload.hovered ? Theme.surfaceAlt : "transparent"
                                    border.color: Theme.border
                                }
                                contentItem: Icon { name: "close"; color: Theme.textDim; size: 10 }
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
                            // Stop, beside the bar it belongs to: a transfer the
                            // user has changed their mind about ends here as well
                            // as in the activity panel, and this is where they are
                            // looking while it runs.
                            Button {
                                id: stopDownload
                                implicitWidth: 18
                                implicitHeight: 18
                                padding: 0
                                hoverEnabled: true
                                ToolTip.visible: hovered
                                ToolTip.text: "Stop"
                                onClicked: if (delegate.session) {
                                    delegate.session.cancelTransfer(model.e2eId)
                                }
                                background: Rectangle {
                                    radius: 4
                                    color: stopDownload.hovered ? Theme.surfaceAlt : "transparent"
                                    border.color: Theme.border
                                }
                                contentItem: Icon { name: "close"; color: Theme.textDim; size: 10 }
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
                            saveDialog.e2eId = model.e2eId
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
                    // Holds the place while the bytes are decoded - but not when
                    // the decode failed: a placeholder that never resolves is a
                    // black rectangle the user cannot tell from a picture.
                    visible: model.hasPicture && preview.status !== Image.Ready
                        && preview.status !== Image.Error
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
                            model.e2eId, model.attName)
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Label {
                    visible: delegate.pictureBroken || preview.status === Image.Error
                    text: "Broken picture"
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                }

            }

            // A voice message: the shape of what was said, how long it runs and
            // what it weighs. The audio is in the account, decoded straight into
            // the speaker; the waveform was drawn from that same audio when the
            // message was stored.
            RowLayout {
                visible: delegate.isVoice
                Layout.fillWidth: true
                spacing: 8

                IconButton {
                    iconName: delegate.voicePlaying ? "close" : "play"
                    tint: Theme.accent
                    onClicked: delegate.session.playVoice(model.e2eId)
                }

                // One bar per slice of the recording, read out of the stored hex
                // account. A message stored without one draws nothing rather
                // than an invented shape.
                Row {
                    id: wave
                    readonly property string hex: model.attWave || ""
                    readonly property int kBars: 32
                    readonly property int kMaxHeight: 26
                    // How much of it has been played, in bars. Only the message
                    // actually playing has any: the rest sit unlit.
                    readonly property int playedBars: {
                        if (!delegate.voicePlaying || !(model.attDurationMs > 0)) {
                            return 0
                        }
                        const fraction = delegate.session.voicePositionMs / model.attDurationMs
                        return Math.round(Math.min(1, Math.max(0, fraction)) * kBars)
                    }
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
                            color: index < wave.playedBars ? Theme.accent : Theme.textDim
                        }
                    }
                    // Pressing the shape plays from there: the drawn audio is the
                    // only place in the bubble that knows where "there" is.
                    TapHandler {
                        onTapped: function(point) {
                            if (!(model.attDurationMs > 0)) {
                                return
                            }
                            const fraction = Math.min(1, Math.max(0, point.position.x / wave.width))
                            delegate.session.playVoice(model.e2eId,
                                Math.round(fraction * model.attDurationMs))
                        }
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Label {
                    // How long it runs, and while it is running how much of it is
                    // left - in the time that is actually still to wait, so a
                    // message played at 2x counts down twice as fast.
                    text: {
                        const durationMs = model.attDurationMs || 0
                        const speed = delegate.session ? delegate.session.voiceSpeed : 1
                        const leftMs = delegate.voicePlaying
                            ? Math.max(0, (durationMs - delegate.session.voicePositionMs) / speed)
                            : durationMs
                        const total = Math.round(leftMs / 1000)
                        const seconds = total % 60
                        return Math.floor(total / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
                    }
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                }
                // What it weighs is footer material; this row is for playing it.
                Item { Layout.fillWidth: true }
                // Playback speed, stepped through by pressing it. It belongs to
                // the session, so the choice holds for the next one too.
                Label {
                    text: (delegate.session ? delegate.session.voiceSpeed : 1) + "x"
                    color: (delegate.session && delegate.session.voiceSpeed > 1)
                        ? Theme.accent : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    // As wide as the widest label it will ever hold: stepping
                    // 1x -> 1.5x otherwise widened the bubble under the cursor.
                    Layout.preferredWidth: Math.ceil(speedWidth.advanceWidth)
                    horizontalAlignment: Text.AlignRight
                    TextMetrics {
                        id: speedWidth
                        font.pixelSize: Theme.fontSmall
                        text: "1.5x"
                    }
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

            // The body. A read-only TextEdit (not a Label) so the user can select
            // text with the mouse and copy it (Ctrl+C); "Copy all" in the context
            // menu copies the message as it was written, markers and all.
            //
            // The document is built from the message rather than handed the
            // message: a body drawn as rich text as it arrived would let a
            // correspondent name a picture in it, and the picture would be
            // fetched - from this machine, over the ordinary internet - as the
            // bubble drew.
            TextEdit {
                id: bodyText
                visible: !delegate.isAttachment && !delegate.isUnsupported
                    && !delegate.isContactRequest && model.text.length > 0
                text: App.markupHtml(model.text, Theme.accent, Theme.surfaceAlt)
                color: Theme.text
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                textFormat: TextEdit.RichText
                selectionColor: Theme.accent
                selectedTextColor: Theme.bg
                Layout.fillWidth: true
                onLinkActivated: function(link) { delegate.activateLink(link) }
                // Where a web address leads is shown before it is followed; the
                // dialog says it again, but the cursor gets there first.
                ToolTip.visible: bodyText.hoveredLink.startsWith("http")
                ToolTip.text: bodyText.hoveredLink
                HoverHandler {
                    cursorShape: bodyText.hoveredLink.length > 0
                        ? Qt.PointingHandCursor : Qt.IBeamCursor
                }
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
                // A Loader does not take its height from the layout it loads: the
                // layout sizes itself to its parent, and its parent is this
                // Loader, which is nothing high until it is told what to be. The
                // buttons were built and given no room, which reads exactly like a
                // bot whose keyboard does not arrive.
                Layout.preferredHeight: active && item ? item.implicitHeight : 0
                sourceComponent: ColumnLayout {
                    id: keysColumn
                    spacing: 4
                    // The rows are held here rather than read straight out of the
                    // delegate: a Repeater inside a loaded component takes nothing
                    // from an array reached through an outer id - measured, the
                    // same expression reads fine everywhere else in this component
                    // and builds no rows at all as a model.
                    property var rows: delegate.keyboardButtons
                    Repeater {
                        model: keysColumn.rows
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
                                        // The press is dimmed until the reply
                                        // lands, so it cannot be sent twice; what
                                        // it is doing is said in the activity
                                        // panel, not written on the message.
                                        delegate.busy = true
                                        busyTimer.restart()
                                        if (modelData.data !== undefined)
                                            delegate.session.sendCallback(modelData.data,
                                                delegate.msgE2eId, modelData.text)
                                        else if (modelData.command !== undefined)
                                            delegate.session.sendCommand(modelData.command, "",
                                                modelData.text)
                                    }
                                    // Pointing-hand cursor over the button.
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    background: Rectangle {
                                        radius: 8
                                        // At rest a shade above the bubble it sits
                                        // on, with a border to match: on the
                                        // bubble's own surface the buttons read as
                                        // more text.
                                        color: kbButton.down ? Theme.accent
                                            : kbButton.hovered ? Theme.bg : Theme.surfaceAlt
                                        border.color: kbButton.hovered
                                            ? Theme.accent : Theme.border2
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

            // Footer: edited marker + time + outgoing status, and for a voice
            // message what it weighs - on this line rather than on the one with
            // the waveform, the speed and the play button.
            RowLayout {
                readonly property bool spread: delegate.isVoice || delegate.isPicture
                    || delegate.reactions.length > 0
                Layout.alignment: spread ? Qt.AlignLeft : Qt.AlignRight
                Layout.fillWidth: spread
                spacing: 4
                Label {
                    visible: (delegate.isVoice || delegate.isPicture) && model.attSize > 0
                    text: delegate.humanSize(model.attSize)
                    color: Theme.textFaint
                    font.pixelSize: 10
                }
                Item {
                    visible: parent.spread
                    Layout.fillWidth: true
                    implicitHeight: 1
                }
                // Reaction chips, on the same line as the time and to the left of
                // it: one per distinct emoji with its count. Dark, like every
                // other surface here: ours the darkest, a contact's a shade
                // lighter, so whose it is reads without a second colour. Tapping
                // a chip toggles our reaction to that emoji.
                Repeater {
                    model: delegate.reactions
                    Rectangle {
                        id: chip
                        required property var modelData
                        // New since this chat was last looked at: it is given the
                        // brand's one accent for a moment and then let go.
                        readonly property bool fresh:
                            delegate.freshReactions.indexOf(modelData.emoji) >= 0
                        height: 18
                        width: chipRow.implicitWidth + 8
                        radius: 4
                        color: modelData.mine ? Theme.deep : Theme.surfaceAlt
                        border.color: modelData.mine ? Theme.border2 : Theme.border
                        // A short neon pulse rather than a steady colour: it says
                        // "this is what you came for" and then leaves the chip
                        // looking like every other one.
                        Rectangle {
                            id: pulse
                            anchors.fill: parent
                            radius: parent.radius
                            color: "transparent"
                            border.color: Theme.neon
                            border.width: 1
                            visible: opacity > 0
                            opacity: 0
                            // The animation names what it drives rather than
                            // taking the property over: written as a value source
                            // it would be fighting the nought above for it, and
                            // which of the two won would be anybody's guess.
                            SequentialAnimation {
                                running: chip.fresh
                                loops: 2
                                NumberAnimation {
                                    target: pulse; property: "opacity"; to: 1; duration: 180
                                }
                                NumberAnimation {
                                    target: pulse; property: "opacity"; to: 0; duration: 420
                                }
                            }
                        }
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 3
                            Label {
                                text: modelData.emoji
                                color: Theme.text
                                font.pixelSize: 12
                                // The emoji font only for what is emoji: it has no
                                // glyphs for plain digits, which came out blank
                                // when everything was forced through it.
                                font.family: delegate.plainText(modelData.emoji)
                                    ? Theme.fontFamily : Theme.emojiFontFamily
                                renderType: Text.NativeRendering
                            }
                            Label {
                                visible: modelData.count > 1
                                text: modelData.count
                                color: Theme.textDim
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        TapHandler {
                            onTapped: delegate.session.react(model.e2eId, modelData.emoji)
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
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
                    border.width: model.status === DeliveryStatus.Preparing ? 1 : 0
                    border.color: Theme.textDim
                    HoverHandler { id: statusHover }
                    ToolTip.visible: statusHover.hovered
                    ToolTip.text: delegate.statusText(model.status)
                }
            }

            // What is happening to a send that has not gone through yet, in the same
            // grey as the chip: a message being tried again says so on itself,
            // instead of looking like one nobody is carrying.
            Label {
                id: tryingLine
                property bool copied: false
                visible: model.outgoing && model.status === DeliveryStatus.Delivering
                    && model.error && model.error.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 2
                text: tryingLine.copied ? "Copied to clipboard" : model.error
                color: tryingLine.copied ? Theme.green : Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Timer { id: tryingCopied; interval: 1500; onTriggered: tryingLine.copied = false }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        if (!delegate.session) {
                            return
                        }
                        delegate.session.copyText(model.error)
                        tryingLine.copied = true
                        tryingCopied.restart()
                    }
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
                // A failure is the one line worth carrying out of the window, so
                // a tap on it puts it on the clipboard and says that it did.
                Label {
                    id: failLine
                    property bool copied: false
                    readonly property string reason: (model.error && model.error.length > 0)
                        ? model.error : "Failed to send"
                    text: failLine.copied ? "Copied to clipboard" : failLine.reason
                    color: failLine.copied ? Theme.green : Theme.danger
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Timer { id: failCopied; interval: 1500; onTriggered: failLine.copied = false }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (!delegate.session) {
                                return
                            }
                            delegate.session.copyText(failLine.reason)
                            failLine.copied = true
                            failCopied.restart()
                        }
                    }
                }
                // Resend covers everything this device can send again by itself.
                Label {
                    id: resendLink
                    visible: model.type === "text" || model.type === "file"
                        || model.type === "image" || model.type === "voice"
                    text: "Resend"
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (model.type === "text") {
                                delegate.session.resendText(
                                    model.msgId, model.text, model.e2eId)
                            } else if (model.type === "voice") {
                                delegate.session.resendVoice(model.msgId, model.e2eId)
                            } else {
                                delegate.session.resendFile(model.msgId, model.e2eId)
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
            onTapped: function(point) {
                delegate.menuAt = Qt.point(point.scenePosition.x, point.scenePosition.y)
                contextMenu.popup()
            }
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onLongPressed: function(point) {
                delegate.menuAt = Qt.point(point.scenePosition.x, point.scenePosition.y)
                contextMenu.popup()
            }
        }
        FileDialog {
            id: pictureSaveDialog
            property string e2eId
            fileMode: FileDialog.SaveFile
            onAccepted: delegate.session.savePictureAs(pictureSaveDialog.e2eId, selectedFile)
        }
        ContextMenu {
            id: contextMenu
            ContextMenuItem {
                id: reactEntry
                // Plain ASCII, no "…" / "&": the default MenuItem renderer draws the
                // ellipsis as a tofu box and treats "&" as a mnemonic, which mangled
                // these labels into "React_" / "React_ view".
                text: "React"
                iconName: "smile"
                visible: delegate.reactable
                height: visible ? implicitHeight : 0
                onTriggered: delegate.reactRequested(model.e2eId,
                    delegate.menuAt.x, delegate.menuAt.y)
            }
            ContextMenuItem {
                // From the account straight to the clipboard: no file in between.
                text: "Copy picture"
                iconName: "copy"
                visible: delegate.pictureUrl.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: delegate.session.copyPicture(model.e2eId)
            }
            ContextMenuItem {
                // A picture lives in the account database; this is how it leaves
                // it as a file.
                text: "Save as"
                iconName: "save"
                visible: delegate.pictureUrl.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: {
                    pictureSaveDialog.currentFile = delegate.session.defaultPictureSaveUrl(
                        model.e2eId, model.attName)
                    pictureSaveDialog.e2eId = model.e2eId
                    pictureSaveDialog.open()
                }
            }
            ContextMenuItem {
                text: "Forward"
                iconName: "forward"
                // Anything a person wrote or sent can be passed on; service notices
                // and requests cannot.
                visible: !delegate.isSystem && !delegate.isUnsupported
                    && !delegate.isContactRequest
                    && model.e2eId && model.e2eId.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: delegate.forwardRequested(model.e2eId)
            }
            ContextMenuItem {
                text: "Reply"
                iconName: "reply"
                // Any real message (text or attachment) can be replied to; service
                // notices, requests and unsupported placeholders cannot.
                visible: !delegate.isSystem && !delegate.isUnsupported
                    && !delegate.isContactRequest
                    && model.e2eId && model.e2eId.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: {
                    var preview = delegate.isAttachment ? (model.attName || "") : (model.text || "")
                    var who = model.outgoing ? "You"
                        : (delegate.session ? delegate.session.activePeerName : "")
                    delegate.session.beginReply(model.e2eId, preview, who)
                }
            }
            ContextMenuItem {
                text: "Copy all"
                iconName: "copy"
                // Only where there is text to copy: an attachment, a request, a
                // picture and a voice message carry none.
                visible: !delegate.isAttachment && !delegate.isContactRequest
                    && delegate.fullText.length > 0
                height: visible ? implicitHeight : 0
                onTriggered: delegate.session.copyText(delegate.fullText)
            }
            ContextMenuItem {
                text: "Edit"
                iconName: "edit"
                visible: delegate.canEdit
                height: visible ? implicitHeight : 0
                onTriggered: delegate.session.beginEdit(model.msgId, model.e2eId, model.text)
            }
            ContextMenuItem {
                text: "Delete"
                iconName: "trash"
                onTriggered: delegate.deleteRequested(model.msgId, model.e2eId, model.outgoing)
            }
        }
    }

    // Native Save dialog: the OS file picker pre-filled with the message's file
    // name, so it resolves any name conflict itself. On accept the download runs in
    // the background with its byte progress shown on this bubble. peer/e2eId/token
    // are snapshotted on open so a recycled delegate cannot misroute the result.
    FileDialog {
        id: saveDialog
        property string peer: ""
        property string e2eId: ""
        property var token: 0
        title: "Save file"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (delegate.session) {
                delegate.session.saveAttachmentToFile(
                    saveDialog.peer, saveDialog.e2eId, "" + saveDialog.selectedFile,
                    saveDialog.token)
            }
        }
    }
}
