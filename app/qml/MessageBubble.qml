// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish
import "Format.js" as Format

Item {
    id: delegate
    property var session: null
    property bool highlighted: false
    readonly property bool held: holdHandler.pressed
        || (contextMenu.item !== null && contextMenu.item.menu.visible)
    readonly property int heldFadeMs: 120
    signal deleteRequested(var msgId, string e2eId, bool outgoing)
    signal groupActionRequested(string command, string postId, string author)
    signal imageRequested(url source, string e2eId, string name)
    signal reactRequested(string e2eId, real sceneX, real sceneY)
    signal forwardRequested(string e2eId)
    signal quoteRequested(var localId)
    signal selectTextRequested(Item text)
    property bool choosing: false
    property bool chosen: false
    signal chosenToggled(var msgId, bool outgoing)
    readonly property int kChoiceSize: 22
    readonly property int kChoiceMark: 14
    readonly property real kChosenTint: 0.18
    signal aliasRequested(string alias)
    signal linkRequested(string url)
    // Taken from the event: mapping through the delegate drops an own bubble's offset.
    property point menuAt: Qt.point(0, 0)
    width: ListView.view ? ListView.view.width : 0
    height: isSystem ? (sysBlock.implicitHeight + 12) : (bubble.height + 4)

    readonly property string kSendScheme: "bz-send:"
    readonly property string kAliasScheme: "bz-alias:"
    readonly property string kCopyScheme: "bz-copy:"
    property bool copiedBlock: false
    Timer { id: copiedBlockFor; interval: 1500; onTriggered: delegate.copiedBlock = false }

    function activateLink(link) {
        if (link.startsWith(delegate.kSendScheme)) {
            if (delegate.session) {
                delegate.session.sendOffered(
                    decodeURIComponent(link.substring(delegate.kSendScheme.length)))
            }
        } else if (link.startsWith(delegate.kAliasScheme)) {
            delegate.aliasRequested(decodeURIComponent(link.substring(delegate.kAliasScheme.length)))
        } else if (link.startsWith(delegate.kCopyScheme)) {
            if (delegate.session) {
                App.copyText(
                    decodeURIComponent(link.substring(delegate.kCopyScheme.length)))
                delegate.copiedBlock = true
                copiedBlockFor.restart()
            }
        } else {
            delegate.linkRequested(link)
        }
    }

    readonly property bool isPicture: model.type === "image"
    readonly property bool isVoice: model.type === "audio"
    readonly property bool voicePlaying: delegate.isVoice && delegate.session
        && delegate.session.voicePlaying === model.e2eId
    readonly property string pictureUrl: (delegate.isPicture && model.hasPicture)
        ? delegate.session.pictureSource(model.e2eId, Pictures.revision) : ""
    readonly property bool pictureBroken: delegate.isPicture && !model.hasPicture
    readonly property bool isAttachment: !delegate.isPicture && !delegate.isVoice
        && ((model.attName && model.attName.length > 0)
            || (model.outgoing && model.type === "file"))
    readonly property bool isUnsupported: model.type === "unsupported"
    readonly property bool isAddFailed: model.type === "contact.failed"
    readonly property bool isRoutingFailed: model.type === "routing.failed"
    readonly property bool isSystem: model.type === "system" || delegate.isAddFailed
        || delegate.isRoutingFailed
    readonly property bool isContactRequest: model.type === "contact.request"
    readonly property bool isContactRequestIncoming: isContactRequest && !model.outgoing

    readonly property var replyInfo: (model.replyTo && model.replyTo.length > 0 && delegate.session)
        ? delegate.session.replyPreview(model.replyTo) : null
    readonly property bool hasReplyQuote: delegate.replyInfo !== null && delegate.replyInfo.found

    readonly property bool reactable: !delegate.isSystem && !delegate.isContactRequest
        && model.e2eId && model.e2eId.length > 0
    readonly property int kStickerMaxEmoji: 3
    readonly property int kStickerOnePixels: 56
    readonly property int kStickerFewPixels: 40
    readonly property int emojiCount: delegate.isSystem || delegate.isAttachment
        || delegate.isContactRequest || (model.replyTo || "").length > 0
        ? 0 : App.emojiCount(model.text || "")
    readonly property bool sticker: delegate.emojiCount > 0
        && delegate.emojiCount <= delegate.kStickerMaxEmoji
    readonly property var reactions: (delegate.session && delegate.reactable
        && delegate.session.reactionsRevision >= 0)
        ? delegate.session.reactionSummary(model.e2eId) : []
    readonly property var freshReactions: (delegate.session && delegate.reactable
        && delegate.session.reactionsRevision >= 0)
        ? delegate.session.reactionsToFlash(model.e2eId) : []

    Rectangle {
        anchors.fill: parent
        visible: delegate.chosen
        color: Theme.green
        opacity: delegate.kChosenTint
    }

    ColumnLayout {
        id: sysBlock
        visible: delegate.isSystem
        anchors.centerIn: parent
        width: parent.width - 80
        spacing: 8

        Label {
            text: model.text
            color: (delegate.isAddFailed || delegate.isRoutingFailed)
                ? Theme.danger : Theme.textDim
            font.pixelSize: Theme.fontSmall
            font.italic: !delegate.isAddFailed && !delegate.isRoutingFailed
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            visible: delegate.isRoutingFailed
            spacing: 8
            Layout.alignment: Qt.AlignHCenter
            MenuButton {
                iconName: "refresh"
                text: qsTr("Try again")
                onClicked: if (delegate.session) {
                    delegate.session.retryRoutingTo(delegate.session.activePeer)
                }
            }
        }
        RowLayout {
            visible: delegate.isAddFailed
            spacing: 8
            Layout.alignment: Qt.AlignHCenter
            MenuButton {
                iconName: "refresh"
                text: qsTr("Try again")
                onClicked: if (delegate.session) { delegate.session.retryContactAdd() }
            }
            MenuButton {
                iconName: "trash"
                danger: true
                text: qsTr("Delete chat")
                onClicked: if (delegate.session) { delegate.session.deleteContact() }
            }
        }
    }

    function parseKeyboard(wire) {
        if (!wire || wire.length === 0) {
            return []
        }
        try { return JSON.parse(wire) } catch (e) { return [] }
    }
    readonly property var keyboardButtons: delegate.parseKeyboard(model.keyboard)
    function keyLines(naturals) {
        const lines = []
        for (let at = 0; at < naturals.length; ++at) {
            const last = lines.length > 0 ? lines[lines.length - 1] : null
            const joined = last ? last.width + delegate.kKeyGap + naturals[at] : 0
            if (last && joined <= delegate.contentRoom) {
                last.keys.push(at)
                last.width = joined
            } else {
                lines.push({keys: [at], width: Math.min(naturals[at], delegate.contentRoom)})
            }
        }
        return lines
    }
    function keyPlace(lines, naturals, index, rowWidth) {
        for (let line = 0; line < lines.length; ++line) {
            const keys = lines[line].keys
            const at = keys.indexOf(index)
            if (at < 0) {
                continue
            }
            const share = Math.max(0, rowWidth - lines[line].width) / keys.length
            const widthOf = key => Math.floor(Math.min(naturals[key], rowWidth) + share)
            let x = 0
            for (let before = 0; before < at; ++before) {
                x += widthOf(keys[before]) + delegate.kKeyGap
            }
            return {line: line, x: x, width: at === keys.length - 1 ? rowWidth - x : widthOf(index)}
        }
        return {line: 0, x: 0, width: 0}
    }
    function shownAddress(link) {
        return link.replace(/</g, "%3C").replace(/>/g, "%3E")
    }

    readonly property string msgE2eId: model.e2eId

    readonly property bool canEdit: model.outgoing && model.type === "text"
        && !delegate.isAttachment
    readonly property string fullText: delegate.isAttachment
        ? (model.attName || "") : (model.text || "")

    // Transient "waiting for the bot" state set when a keyboard button is
    // tapped; cleared when the message's content changes (the reply edited it)
    // or after a short fallback timeout, so a tap always visibly registers.
    property bool busy: false
    readonly property string contentKey: (model.text || "") + "" + (model.keyboard || "")
    onContentKeyChanged: delegate.busy = false
    Timer { id: busyTimer; interval: 6000; onTriggered: delegate.busy = false }
    property bool pooled: false
    readonly property bool textSelecting: !!(delegate.ListView.view && delegate.ListView.view.selectingText)
    ListView.onPooled: {
        if (delegate.ListView.view && delegate.ListView.view.selectingText === bodyText) {
            bodyText.deselect()
            delegate.ListView.view.selectingText = null
        }
        pooled = true
        springBack.stop()
        pull = 0
        copiedBlock = false
        copiedBlockFor.stop()
        busy = false
        busyTimer.stop()
    }
    ListView.onReused: {
        pooled = false
        content.ensurePolished()
    }

    property real pull: 0
    readonly property int kActionPull: 64
    readonly property int kMaxPull: 96
    readonly property real kPullResistance: 0.3
    readonly property bool actionable: !delegate.isSystem && !delegate.isContactRequest
        && !!model.e2eId && model.e2eId.length > 0
    function reply() {
        const preview = delegate.isAttachment ? (model.attName || "") : (model.text || "")
        const who = model.outgoing ? "You"
            : (model.author ? model.author
                : (delegate.session ? delegate.session.activePeerName : ""))
        delegate.session.beginReply(model.e2eId, preview, who)
    }
    // A drag handler below a popup still sees a drag that starts on the popup, and takes it over.
    readonly property bool covered: !!delegate.Overlay.overlay && delegate.Overlay.overlay.visible
    // Android's back gesture begins as a press at a side of the window, before the system takes it.
    function systemsTouch(scene) {
        return App.inBackGestureEdge(scene.x, delegate.Window.width)
    }
    property bool swipeFromEdge: false
    DragHandler {
        id: swipeHandler
        target: null
        enabled: delegate.actionable && !delegate.textSelecting && !delegate.covered
            && !delegate.choosing
        acceptedDevices: PointerDevice.TouchScreen
        yAxis.enabled: false
        onActiveTranslationChanged: {
            const drawn = Math.abs(activeTranslation.x)
            const eased = drawn <= delegate.kActionPull ? drawn
                : delegate.kActionPull + (drawn - delegate.kActionPull) * delegate.kPullResistance
            delegate.pull = Math.sign(activeTranslation.x) * Math.min(delegate.kMaxPull, eased)
        }
        onActiveChanged: {
            if (active) {
                delegate.swipeFromEdge = delegate.systemsTouch(swipeHandler.centroid.scenePressPosition)
                springBack.stop()
                return
            }
            if (delegate.swipeFromEdge) {
                springBack.start()
                return
            }
            if (delegate.pull <= -delegate.kActionPull) {
                delegate.reply()
            } else if (delegate.pull >= delegate.kActionPull) {
                delegate.forwardRequested(model.e2eId)
            }
            springBack.start()
        }
    }
    NumberAnimation {
        id: springBack
        target: delegate
        property: "pull"
        to: 0
        duration: 180
        easing.type: Easing.OutCubic
    }
    Icon {
        readonly property bool armed: Math.abs(delegate.pull) >= delegate.kActionPull
        visible: delegate.pull !== 0
        name: delegate.pull < 0 ? "reply" : "forward"
        size: 20
        color: armed ? Theme.green : Theme.textDim
        opacity: Math.min(1, Math.abs(delegate.pull) / delegate.kActionPull)
        scale: armed ? 1.15 : 0.6 + 0.4 * opacity
        Behavior on scale { NumberAnimation { duration: 90 } }
        x: delegate.pull < 0
            ? Math.min(bubble.x + bubble.width + delegate.pull + 8, delegate.width - width - 12)
            : Math.max(bubble.x + delegate.pull - 8 - width, 12)
        anchors.verticalCenter: bubble.verticalCenter
    }

    function statusColor(s) {
        if (s === DeliveryStatus.Delivering) return Theme.textDim
        if (s === DeliveryStatus.AtRecipientServer) return Theme.warn
        if (s === DeliveryStatus.Delivered) return Theme.success
        if (s === DeliveryStatus.Failed) return Theme.danger
        return "transparent"
    }
    readonly property bool toSelf: delegate.session !== null
        && delegate.session.isSavedChat(delegate.session.activePeer)
    readonly property bool inGroup: delegate.session !== null && delegate.session.activeIsGroup
        && !model.outgoing && !delegate.isSystem
    readonly property bool fromMember: delegate.inGroup && (model.author || "").length > 0
    readonly property bool fromHost: delegate.inGroup && model.fromHost === true
    readonly property bool onlyForMe: delegate.inGroup && !delegate.fromMember && !delegate.fromHost
    readonly property int letterSize: 30
    readonly property real kBubbleShare: 0.72
    readonly property int kBubbleInset: 20
    readonly property real contentRoom: delegate.width * kBubbleShare - kBubbleInset
    readonly property int kKeyHeight: 34
    readonly property int kKeyGap: 4
    readonly property int kKeyPadding: 10
    readonly property int kErrorPixelSize: 11
    readonly property int kErrorLines: 2
    readonly property int kResendTarget: 28
    readonly property int kResendIcon: 16
    readonly property int letterGap: 8
    readonly property var invitePlates: delegate.session && model.text
        ? delegate.session.invitesIn(model.text) : []
    function statusText(s) {
        if (s === DeliveryStatus.Delivering) {
            return delegate.toSelf ? qsTr("Sending…")
                                   : qsTr("Sending to the recipient's server…")
        }
        if (s === DeliveryStatus.AtRecipientServer) return qsTr("Handed to the recipient's server")
        if (s === DeliveryStatus.Delivered) return qsTr("Delivered")
        if (s === DeliveryStatus.Failed) return qsTr("Failed to send")
        return qsTr("Preparing an address to send from…")
    }

    Rectangle {
        visible: delegate.fromMember
        transform: Translate { x: delegate.pull }
        width: delegate.letterSize
        height: delegate.letterSize
        radius: width / 2
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.bottom: bubble.bottom
        color: delegate.fromMember ? delegate.session.letterFill(model.author) : "transparent"
        Label {
            anchors.centerIn: parent
            text: delegate.fromMember ? delegate.session.letterOf(model.author) : ""
            color: Theme.text
            font.pixelSize: Theme.fontSmall + 1
            font.weight: Font.DemiBold
        }
    }

    TapHandler {
        enabled: !delegate.isSystem && !delegate.choosing
        // A touch carries no button, so a right-button handler would take every tap.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedButtons: Qt.RightButton
        onTapped: function(point) { delegate.openMenu(point.scenePosition) }
    }
    TapHandler {
        id: holdHandler
        enabled: !delegate.isSystem && !delegate.textSelecting && !delegate.choosing
        acceptedButtons: Qt.LeftButton
        onLongPressed: {
            if (!delegate.systemsTouch(holdHandler.point.scenePressPosition)) {
                delegate.openMenu(holdHandler.point.scenePosition)
            }
        }
    }
    function openMenu(scene) {
        delegate.menuAt = Qt.point(scene.x, scene.y)
        contextMenu.active = true
        contextMenu.item.menu.popup()
    }
    function within(item, scene) {
        return item.visible && item.contains(item.mapFromItem(null, scene.x, scene.y))
    }
    function onTappablePart(scene) {
        const parts = [replyQuote, preview, wave, speedChip, resendLink]
        for (let i = 0; i < reactionChips.count; ++i) {
            parts.push(reactionChips.itemAt(i))
        }
        return parts.some(part => delegate.within(part, scene))
    }
    TapHandler {
        enabled: App.mobile && !delegate.isSystem && !delegate.textSelecting && !delegate.choosing
        acceptedButtons: Qt.LeftButton
        onTapped: function(point) {
            const scene = point.scenePosition
            if (delegate.systemsTouch(point.scenePressPosition)) {
                return
            }
            if (bodyText.visible) {
                const at = bodyText.mapFromItem(null, scene.x, scene.y)
                const link = bodyText.linkAt(at.x, at.y)
                if (link.length > 0) {
                    delegate.activateLink(link)
                    return
                }
            }
            if (!delegate.within(bubble, scene)) {
                delegate.openMenu(scene)
            } else if (delegate.reactable && !delegate.onTappablePart(scene)) {
                delegate.reactRequested(model.e2eId, scene.x, scene.y)
            }
        }
    }

    Rectangle {
        id: bubble
        visible: !delegate.isSystem
        transform: Translate { x: delegate.pull }
        // One x, not side anchors: a reused bubble changing sides held both anchors and stretched.
        x: model.outgoing ? parent.width - width - 12
            : (delegate.inGroup ? 12 + delegate.letterSize + delegate.letterGap : 12)
        width: Math.min(Math.max(content.implicitWidth + delegate.kBubbleInset, 80),
            delegate.width * delegate.kBubbleShare)
        height: content.implicitHeight + 14
        radius: 12
        color: delegate.sticker ? "transparent"
            : model.outgoing ? (delegate.held ? Theme.bubbleOutHeld : Theme.bubbleOut)
            : (delegate.held ? Theme.bubbleInHeld : Theme.bubbleIn)
        Behavior on color {
            enabled: !delegate.pooled
            ColorAnimation { duration: delegate.heldFadeMs }
        }
        border.width: delegate.highlighted ? 2
            : (delegate.isContactRequestIncoming || delegate.onlyForMe ? 1 : 0)
        border.color: delegate.isContactRequestIncoming ? Theme.green
            : (delegate.onlyForMe && !delegate.highlighted ? Theme.text : Theme.accent)
        Behavior on border.width {
            enabled: !delegate.pooled
            NumberAnimation { duration: 220 }
        }

        ColumnLayout {
            id: content
            x: 10
            y: 7
            width: parent.width - 20
            spacing: 4

            Label {
                visible: delegate.fromMember
                text: model.author || ""
                color: Theme.authorName
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                visible: delegate.onlyForMe
                text: qsTr("Only you")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                Layout.fillWidth: true
            }

            RowLayout {
                visible: model.forwarded === true
                Layout.fillWidth: true
                Layout.bottomMargin: 2
                spacing: 5
                Icon { name: "forwarded"; color: Theme.textDim; size: 13 }
                Label {
                    text: qsTr("Forwarded")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                id: replyQuote
                visible: delegate.hasReplyQuote
                implicitWidth: replyCol.implicitWidth + 16
                Layout.fillWidth: true
                Layout.preferredHeight: delegate.hasReplyQuote ? replyCol.implicitHeight + 8 : 0
                radius: 6
                color: Theme.surface
                border.color: Theme.border
                border.width: 1
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
                        readonly property int kQuoteLines: 2
                        text: delegate.replyInfo ? App.markupPlain(delegate.replyInfo.text) : ""
                        color: Theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        maximumLineCount: kQuoteLines
                        Layout.fillWidth: true
                    }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: delegate.quoteRequested(delegate.replyInfo.localId)
                }
            }

            ColumnLayout {
                visible: delegate.isContactRequest
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: delegate.isContactRequestIncoming
                        ? qsTr("wants to add you as a contact")
                        : qsTr("Contact request sent")
                    color: Theme.green
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: model.text.length > 0
                    text: App.markupPlain(model.text)
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Button {
                    id: agreeButton
                    focusPolicy: Theme.pressFocus
                    visible: delegate.isContactRequestIncoming && delegate.session
                        && delegate.session.contactsRevision >= 0
                        && delegate.session.contactCanAccept(delegate.session.activePeer)
                    readonly property bool inFlight: delegate.session
                        && delegate.session.contactsRevision >= 0
                        && (delegate.session.acceptingContact === delegate.session.activePeer
                            || delegate.session.contactAgreeing(delegate.session.activePeer))
                    text: inFlight ? qsTr("Agreeing\u2026") : qsTr("Agree")
                    enabled: !inFlight
                    hoverEnabled: true
                    leftPadding: 16
                    rightPadding: 16
                    onClicked: if (delegate.session) { delegate.session.acceptContact() }
                    background: Rectangle {
                        radius: 8
                        color: !agreeButton.enabled ? Theme.surfaceAlt
                            : (agreeButton.down ? Qt.darker(Theme.green, 1.2)
                            : (agreeButton.hovered ? Qt.darker(Theme.green, 1.1) : Theme.green))
                    }
                    contentItem: IconLabel {
                        name: "check"
                        color: agreeButton.enabled ? Theme.bg : Theme.textDim
                        weight: Font.Medium
                    }
                }
            }

            Loader {
                active: delegate.isAttachment
                visible: active
                Layout.fillWidth: true
                sourceComponent: ColumnLayout {
                    spacing: 2
                    Label { text: "" + model.attName; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { visible: model.attSize > 0; text: Format.bytes(model.attSize); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    TextMetrics {
                        id: stageMetrics
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("Publishing the address")
                    }

                    ColumnLayout {
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
                            Button {
                                id: stopUpload
                                focusPolicy: Theme.pressFocus
                                implicitWidth: 18
                                implicitHeight: 18
                                padding: 0
                                hoverEnabled: true
                                Accessible.name: qsTr("Stop")
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
                            text: model.transferStage.length > 0 ? model.transferStage
                                : qsTr("Sending\u2026")
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                    ColumnLayout {
                        visible: !model.outgoing
                            && (model.downloading || model.transferStage.length > 0)
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
                                indeterminate: model.downloadTotal <= 0
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
                            Button {
                                id: stopDownload
                                focusPolicy: Theme.pressFocus
                                implicitWidth: 18
                                implicitHeight: 18
                                padding: 0
                                hoverEnabled: true
                                Accessible.name: qsTr("Stop")
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
                            text: model.downloadTotal > 0
                                ? (Format.bytes(model.downloadReceived) + " / "
                                    + Format.bytes(model.downloadTotal))
                                : (model.transferStage.length > 0
                                    ? model.transferStage
                                    : qsTr("Connecting over I2P…"))
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                    CopyableError {
                        visible: model.downloadError.length > 0
                        Layout.fillWidth: true
                        reason: qsTr("Save failed: %1").arg(model.downloadError)
                        session: delegate.session
                    }
                    Button {
                        id: saveButton
                        focusPolicy: Theme.pressFocus
                        visible: !model.outgoing && !model.downloading
                        readonly property bool savedExists: model.savedPath.length > 0
                            && delegate.session && delegate.session.fileExists(model.savedPath)
                        text: savedExists ? qsTr("Open") : qsTr("Save")
                        onClicked: {
                            if (model.savedPath.length > 0 && delegate.session
                                    && delegate.session.fileExists(model.savedPath)) {
                                delegate.session.showInFolder(model.savedPath)
                                return
                            }
                            // In SaveFile mode only currentFile pre-fills the suggested name.
                            saveDialog.peer = delegate.session.activePeer
                            saveDialog.e2eId = model.e2eId
                            saveDialog.token = model.msgId
                            saveDialog.currentFile = delegate.session.defaultSaveUrl(model.attName)
                            saveDialog.open()
                        }
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                        contentItem: IconLabel {
                            name: saveButton.savedExists ? "folder" : "save"
                            color: Theme.accent
                        }
                    }
                }
            }

            ColumnLayout {
                visible: delegate.isPicture
                Layout.fillWidth: true
                spacing: 2

                Rectangle {
                    visible: model.hasPicture && preview.status !== Image.Ready
                        && preview.status !== Image.Error
                    Layout.preferredWidth: Math.min(preview.maxEdge, delegate.contentRoom)
                    Layout.preferredHeight: Math.round(Layout.preferredWidth * 0.6)
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
                    readonly property int maxEdge: 320
                    readonly property real fit: implicitWidth > 0 && implicitHeight > 0
                        ? Math.min(1, maxEdge / implicitWidth, maxEdge / implicitHeight,
                            delegate.contentRoom / implicitWidth)
                        : 0
                    Layout.preferredWidth: implicitWidth * fit
                    Layout.preferredHeight: implicitHeight * fit
                    sourceSize.width: maxEdge * 2
                    TapHandler {
                        onTapped: delegate.imageRequested(delegate.pictureUrl,
                            model.e2eId, model.attName)
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                CopyableError {
                    visible: delegate.pictureBroken || preview.status === Image.Error
                    reason: qsTr("Broken picture")
                    session: delegate.session
                }

            }

            RowLayout {
                visible: delegate.isVoice
                Layout.fillWidth: true
                spacing: 8

                IconButton {
                    iconName: delegate.voicePlaying ? "close" : "play"
                    tint: Theme.accent
                    onClicked: delegate.session.playVoice(model.e2eId)
                }

                Row {
                    id: wave
                    readonly property string hex: model.attWave || ""
                    readonly property int kBars: 32
                    readonly property int kMaxHeight: 26
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
                        model: wave.visible ? wave.kBars : 0
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
                            color: index < wave.playedBars ? Theme.neon : Theme.textDim
                        }
                    }
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
                    text: {
                        const durationMs = model.attDurationMs || 0
                        const speed = delegate.session ? delegate.session.voiceSpeed : 1
                        const leftMs = delegate.voicePlaying
                            ? Math.max(0, (durationMs - delegate.session.voicePositionMs) / speed)
                            : durationMs
                        return Format.minutesSeconds(Math.round(leftMs / 1000))
                    }
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                }
                Item { Layout.fillWidth: true }
                Label {
                    id: speedChip
                    text: qsTr("%1x").arg(delegate.session ? delegate.session.voiceSpeed : 1)
                    color: (delegate.session && delegate.session.voiceSpeed > 1)
                        ? Theme.accent : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    Layout.preferredWidth: Math.ceil(speedWidth.advanceWidth)
                    horizontalAlignment: Text.AlignRight
                    TextMetrics {
                        id: speedWidth
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("1.5x")
                    }
                    TapHandler { onTapped: delegate.session.cycleVoiceSpeed() }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            Repeater {
                model: delegate.invitePlates
                Rectangle {
                    Layout.fillWidth: true
                    implicitWidth: plateRow.implicitWidth + 16
                    implicitHeight: plateRow.implicitHeight + 16
                    radius: Theme.radiusSmall
                    color: Theme.surfaceAlt
                    border.color: Theme.border2
                    RowLayout {
                        id: plateRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                text: modelData.name || qsTr("Contact")
                                color: Theme.text
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: modelData.fingerprint.slice(0, 16) + "…"
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        MenuButton {
                            iconName: "person"
                            text: qsTr("Write")
                            onClicked: App.openLink(modelData.uri)
                        }
                    }
                }
            }

            TextEdit {
                id: bodyText
                visible: !delegate.isAttachment
                    && !delegate.isContactRequest && model.text.length > 0
                    && (delegate.invitePlates.length === 0
                        || delegate.session.withoutInvites(model.text).length > 0)
                text: App.markupHtml(delegate.invitePlates.length > 0
                        ? delegate.session.withoutInvites(model.text) : model.text,
                    Theme.accent, Theme.surfaceAlt, Theme.bg,
                    delegate.isUnsupported ? Theme.danger : Theme.text)
                color: delegate.isUnsupported ? Theme.danger : Theme.text
                enabled: !App.mobile
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                textFormat: TextEdit.RichText
                selectionColor: Theme.accent
                selectedTextColor: Theme.bg
                Layout.fillWidth: true
                onLinkActivated: function(link) { delegate.activateLink(link) }
                ToolTip.visible: !App.mobile && bodyText.hoveredLink.startsWith("http")
                ToolTip.text: delegate.shownAddress(bodyText.hoveredLink)
                HoverHandler {
                    cursorShape: bodyText.hoveredLink.length > 0
                        ? Qt.PointingHandCursor : Qt.IBeamCursor
                }
                Binding on font.pixelSize {
                    when: delegate.sticker
                    value: delegate.emojiCount === 1
                        ? delegate.kStickerOnePixels : delegate.kStickerFewPixels
                    restoreMode: Binding.RestoreBindingOrValue
                }
                Binding on renderType {
                    when: delegate.sticker
                    value: Text.NativeRendering
                    restoreMode: Binding.RestoreBindingOrValue
                }
            }

            Label {
                visible: delegate.copiedBlock
                text: qsTr("Copied to clipboard")
                color: Theme.green
                font.pixelSize: Theme.fontSmall
                Layout.topMargin: 2
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Loader {
                active: !model.outgoing && delegate.keyboardButtons.length > 0
                visible: active
                Layout.fillWidth: true
                Layout.topMargin: 2
                // A Loader in a layout takes no height from what it loads.
                Layout.preferredHeight: active && item ? item.implicitHeight : 0
                sourceComponent: ColumnLayout {
                    id: keysColumn
                    spacing: delegate.kKeyGap
                    // A Repeater in a loaded component builds no rows from an array read through an outer id.
                    property var rows: delegate.keyboardButtons
                    Repeater {
                        model: keysColumn.rows
                        Item {
                            id: keyRow
                            Layout.fillWidth: true
                            readonly property var naturals: {
                                const out = []
                                for (let i = 0; i < keys.count; ++i) {
                                    const key = keys.itemAt(i)
                                    out.push(key ? key.natural : 0)
                                }
                                return out
                            }
                            readonly property var lines: delegate.keyLines(naturals)
                            readonly property var lineTops: {
                                const tops = []
                                let top = 0
                                for (const line of lines) {
                                    tops.push(top)
                                    let tallest = delegate.kKeyHeight
                                    for (const at of line.keys) {
                                        const key = keys.itemAt(at)
                                        if (key) {
                                            tallest = Math.max(tallest, key.height)
                                        }
                                    }
                                    top += tallest + delegate.kKeyGap
                                }
                                tops.push(top)
                                return tops
                            }
                            implicitWidth: lines.reduce((widest, line) => Math.max(widest, line.width), 0)
                            implicitHeight: lines.length > 0
                                ? lineTops[lines.length] - delegate.kKeyGap : 0
                            Repeater {
                                id: keys
                                model: modelData
                                Button {
                                    id: kbButton
                                    focusPolicy: Theme.pressFocus
                                    readonly property real natural:
                                        Math.ceil(keyLabel.implicitWidth) + leftPadding + rightPadding
                                    readonly property var place: delegate.keyPlace(
                                        keyRow.lines, keyRow.naturals, index, keyRow.width)
                                    x: place.x
                                    y: keyRow.lineTops[place.line] || 0
                                    width: place.width
                                    height: Math.max(delegate.kKeyHeight, implicitHeight)
                                    leftPadding: delegate.kKeyPadding
                                    rightPadding: delegate.kKeyPadding
                                    text: modelData.text
                                    hoverEnabled: true
                                    enabled: !delegate.busy
                                    opacity: delegate.busy ? 0.5 : 1.0
                                    onClicked: {
                                        delegate.busy = true
                                        busyTimer.restart()
                                        if (modelData.data !== undefined)
                                            delegate.session.sendCallback(modelData.data,
                                                delegate.msgE2eId, modelData.text)
                                        else if (modelData.command !== undefined)
                                            delegate.session.sendCommand(modelData.command, "",
                                                modelData.text)
                                    }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    background: Rectangle {
                                        radius: 8
                                        color: kbButton.down ? Theme.accent
                                            : kbButton.hovered ? Theme.bg : Theme.surfaceAlt
                                        border.color: kbButton.hovered
                                            ? Theme.accent : Theme.border2
                                        Behavior on color { ColorAnimation { duration: 90 } }
                                    }
                                    contentItem: Label {
                                        id: keyLabel
                                        text: kbButton.text
                                        color: kbButton.down ? Theme.bg : Theme.accent
                                        font.weight: Font.Medium
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                readonly property bool spread: delegate.isVoice || delegate.isPicture
                    || delegate.reactions.length > 0
                Layout.alignment: spread ? Qt.AlignLeft : Qt.AlignRight
                Layout.fillWidth: spread
                spacing: 4
                Label {
                    visible: (delegate.isVoice || delegate.isPicture) && model.attSize > 0
                    text: Format.bytes(model.attSize)
                    color: Theme.textFaint
                    font.pixelSize: 10
                }
                Item {
                    visible: parent.spread
                    Layout.fillWidth: true
                    implicitHeight: 1
                }
                Repeater {
                    id: reactionChips
                    model: delegate.reactions
                    Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool fresh:
                            delegate.freshReactions.indexOf(modelData.emoji) >= 0
                        height: 18
                        width: chipRow.implicitWidth + 8
                        radius: 4
                        color: modelData.mine ? Theme.reactionMine : Theme.surfaceAlt
                        border.color: modelData.mine ? Theme.reactionMineEdge : Theme.border
                        Rectangle {
                            id: pulse
                            anchors.fill: parent
                            radius: parent.radius
                            color: "transparent"
                            border.color: Theme.neon
                            border.width: 1
                            visible: opacity > 0
                            opacity: 0
                            // target/property, not a value source: that would fight the binding above.
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
                                color: modelData.mine ? Theme.reactionMineInk : Theme.text
                                font.pixelSize: 12
                                font.family: Theme.reactionFont(modelData.emoji)
                                renderType: Text.NativeRendering
                            }
                            Label {
                                visible: modelData.count > 1
                                text: modelData.count
                                color: modelData.mine ? Theme.reactionMineInk : Theme.textDim
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
                    text: qsTr("edited")
                    color: Theme.textDim
                    font.pixelSize: 10
                    font.italic: true
                }
                Label {
                    text: model.time ? new Date(model.time).toLocaleTimeString(Qt.locale(), "hh:mm") : ""
                    color: Theme.textDim
                    font.pixelSize: 10
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

            CopyableError {
                visible: model.outgoing && model.status === DeliveryStatus.Delivering
                    && model.error && model.error.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 2
                reason: model.error
                textColor: Theme.textDim
                session: delegate.session
                font.pixelSize: delegate.kErrorPixelSize
                maximumLineCount: delegate.kErrorLines
                elide: Text.ElideRight
            }

            RowLayout {
                visible: model.outgoing && model.status === DeliveryStatus.Failed
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 8
                CopyableError {
                    Layout.fillWidth: true
                    reason: (model.error && model.error.length > 0)
                        ? model.error : qsTr("Failed to send")
                    session: delegate.session
                    font.pixelSize: delegate.kErrorPixelSize
                    maximumLineCount: delegate.kErrorLines
                    elide: Text.ElideRight
                }
                Item {
                    id: resendLink
                    visible: model.type === "text" || model.type === "file"
                        || model.type === "image" || model.type === "audio"
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Resend")
                    implicitWidth: delegate.kResendTarget
                    implicitHeight: delegate.kResendTarget
                    Icon {
                        anchors.centerIn: parent
                        name: "refresh"
                        size: delegate.kResendIcon
                        color: Theme.accent
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (model.type === "text") {
                                delegate.session.resendText(
                                    model.msgId, model.text, model.e2eId)
                            } else if (model.type === "audio") {
                                delegate.session.resendVoice(model.msgId, model.e2eId)
                            } else if (model.type === "image") {
                                delegate.session.resendPicture(model.msgId, model.e2eId)
                            } else {
                                delegate.session.resendFile(model.msgId, model.e2eId)
                            }
                        }
                    }
                }
            }
        }

        FileDialog {
            id: pictureSaveDialog
            property string e2eId
            fileMode: FileDialog.SaveFile
            onAccepted: delegate.session.savePictureAs(pictureSaveDialog.e2eId, selectedFile)
        }
        Loader {
            id: contextMenu
            active: false
            // Under Menu itself, its own delegate property would hide this bubble's id.
            sourceComponent: Item {
                property alias menu: menu
                ContextMenu {
                    id: menu
                    parent: bubble
                    ContextMenuItem {
                        text: qsTr("React")
                        iconName: "smile"
                        visible: delegate.reactable
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.reactRequested(model.e2eId,
                            delegate.menuAt.x, delegate.menuAt.y)
                    }
                    ContextMenuItem {
                        text: qsTr("Copy picture")
                        iconName: "copy"
                        visible: delegate.pictureUrl.length > 0
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.session.copyPicture(model.e2eId)
                    }
                    ContextMenuItem {
                        text: qsTr("Save as")
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
                        text: qsTr("Forward")
                        iconName: "forward"
                        visible: delegate.actionable && !delegate.isUnsupported
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.forwardRequested(model.e2eId)
                    }
                    ContextMenuItem {
                        text: qsTr("Reply")
                        iconName: "reply"
                        visible: delegate.actionable
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.reply()
                    }
                    ContextMenuItem {
                        text: qsTr("Copy all")
                        iconName: "copy"
                        visible: !delegate.isAttachment && !delegate.isContactRequest
                            && delegate.fullText.length > 0
                        height: visible ? implicitHeight : 0
                        onTriggered: App.copyText(delegate.fullText)
                    }
                    ContextMenuItem {
                        text: qsTr("Select text")
                        iconName: "cursor"
                        visible: App.mobile && bodyText.visible
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.selectTextRequested(bodyText)
                    }
                    ContextMenuItem {
                        text: qsTr("Edit")
                        iconName: "edit"
                        visible: delegate.canEdit
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.session.beginEdit(model.msgId, model.e2eId, model.text)
                    }
                    ContextMenuItem {
                        text: qsTr("Request contact")
                        iconName: "person"
                        visible: delegate.fromMember && model.contactable === true
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.session.sendCommand("contact", model.e2eId, text)
                    }
                    ContextMenuItem {
                        text: qsTr("Exclude")
                        iconName: "signout"
                        visible: delegate.fromMember && delegate.session.activeGroupAdmin
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.groupActionRequested("exclude", model.e2eId, model.author)
                    }
                    ContextMenuItem {
                        text: qsTr("Block")
                        iconName: "block"
                        visible: delegate.fromMember && delegate.session.activeGroupAdmin
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.groupActionRequested("block", model.e2eId, model.author)
                    }
                    ContextMenuItem {
                        text: qsTr("Delete for everyone")
                        iconName: "trash"
                        visible: (delegate.fromMember || delegate.fromHost) && delegate.session.activeGroupAdmin
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.groupActionRequested("delete", model.e2eId, model.author)
                    }
                    ContextMenuItem {
                        text: qsTr("Select")
                        iconName: "check"
                        visible: delegate.actionable
                        height: visible ? implicitHeight : 0
                        onTriggered: delegate.chosenToggled(model.msgId, model.outgoing)
                    }
                    ContextMenuItem {
                        text: qsTr("Delete")
                        iconName: "trash"
                        onTriggered: delegate.deleteRequested(model.msgId, model.e2eId, model.outgoing)
                    }
                }
            }
        }
    }

    // Snapshotted on open: a reused delegate would misroute the result.
    FileDialog {
        id: saveDialog
        property string peer: ""
        property string e2eId: ""
        property var token: 0
        title: qsTr("Save file")
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (delegate.session) {
                delegate.session.saveAttachmentToFile(
                    saveDialog.peer, saveDialog.e2eId, "" + saveDialog.selectedFile,
                    saveDialog.token)
            }
        }
    }

    Rectangle {
        visible: delegate.choosing && delegate.actionable
        width: delegate.kChoiceSize
        height: delegate.kChoiceSize
        radius: width / 2
        x: model.outgoing ? bubble.x - width - delegate.kChoiceSize / 2
            : bubble.x + bubble.width + delegate.kChoiceSize / 2
        anchors.verticalCenter: bubble.verticalCenter
        color: delegate.chosen ? Theme.green : "transparent"
        border.color: Theme.green
        border.width: 2
        Icon {
            anchors.centerIn: parent
            visible: delegate.chosen
            name: "check"
            color: Theme.bg
            size: delegate.kChoiceMark
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: delegate.choosing && delegate.actionable
        onClicked: delegate.chosenToggled(model.msgId, model.outgoing)
    }
}
