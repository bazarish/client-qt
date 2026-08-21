import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Item {
    id: root
    property var session: null
    // Set by the window when the chat is the only pane.
    property bool narrow: false
    signal contactInfoRequested()
    signal callRequested()

    // The message awaiting delete confirmation (set when a bubble asks to delete).
    property var pendingDeleteId: null
    property string pendingDeleteProtocol: ""
    property bool pendingDeleteOutgoing: false

    // Humanises a "yyyy-MM-dd" day key into a date-separator label.
    function formatDaySection(iso) {
        if (!iso || iso.length < 10) {
            return ""
        }
        const parts = iso.split("-")
        const d = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]))
        const now = new Date()
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const diff = Math.round((today.getTime() - d.getTime()) / 86400000)
        if (diff === 0) {
            return "Today"
        }
        if (diff === 1) {
            return "Yesterday"
        }
        return d.toLocaleDateString(Qt.locale(), "d MMMM yyyy")
    }

    function confirmDeleteMessage(msgId, protocolId, outgoing) {
        root.pendingDeleteId = msgId
        root.pendingDeleteProtocol = protocolId
        root.pendingDeleteOutgoing = outgoing
        deleteMessageDialog.open()
    }

    // A message counts as read only on a genuine read: this view is open, the app
    // window is focused, and the message is within the visible scroll area. We ack
    // the newest incoming message at or before the bottom-most visible row.
    function markVisibleRead() {
        if (!session || !visible || messages.count === 0) {
            return
        }
        if (Qt.application.state !== Qt.ApplicationActive) {
            return
        }
        var row = messages.indexAt(messages.width / 2, messages.contentY + messages.height - 2)
        if (row < 0) {
            row = messages.count - 1  // content shorter than the view: all visible
        }
        session.markReadThroughRow(row)
    }
    // Reading also resumes when the app regains focus on an already-open chat.
    Connections {
        target: Qt.application
        function onStateChanged() { root.markVisibleRead() }
    }

    // A failed file whose saved source is gone: let the user pick a file to send.
    Connections {
        target: root.session
        function onResendFilePickRequested() { resendPickDialog.open() }
        // Search jump: scroll the chosen message into view and flash it.
        function onScrollToMessage(messageId) {
            Qt.callLater(function() {
                const r = messages.model ? messages.model.rowForId(messageId) : -1
                if (r >= 0) {
                    messages.autoScrolling = true
                    messages.positionViewAtIndex(r, ListView.Center)
                    messages.autoScrolling = false
                    messages.highlightId = messageId
                    highlightTimer.restart()
                }
            })
        }
        // Open-at-first-unread: land on the first unread message (near the top) and
        // briefly highlight the unread tail, then read what is on screen.
        function onScrollToUnread(firstUnreadId) {
            Qt.callLater(function() {
                pinTimer.stop()  // cancel any pin-to-bottom from the model reset
                const r = messages.model ? messages.model.rowForId(firstUnreadId) : -1
                if (r >= 0) {
                    messages.stickToBottom = false
                    messages.autoScrolling = true
                    messages.positionViewAtIndex(r, ListView.Beginning)
                    messages.autoScrolling = false
                    messages.unreadFlashFromId = firstUnreadId
                    unreadFlashTimer.restart()
                }
                Qt.callLater(root.markVisibleRead)
            })
        }
        function onScrollToBottom() { messages.scrollToEnd() }
    }
    FileDialog {
        id: resendPickDialog
        onAccepted: { resendSendOptions.fileUrl = selectedFile; resendSendOptions.open() }
    }
    FileSendDialog {
        id: resendSendOptions
        session: root.session
    }
    Timer { id: highlightTimer; interval: 1800; onTriggered: messages.highlightId = -1 }
    // Clears the brief unread highlight a couple of seconds after opening at the
    // first unread message.
    Timer { id: unreadFlashTimer; interval: 2500; onTriggered: messages.unreadFlashFromId = -1 }

    SearchPopup {
        id: searchPopup
        session: root.session
        anchors.centerIn: Overlay.overlay
        onJumpRequested: function(messageId) {
            close()
            if (root.session) {
                root.session.openConversationAtMessage(root.session.activePeer, messageId)
            }
        }
    }

    // Single shared reaction picker, opened by any bubble with its message's
    // protocol id (so the chat pays no popup per row).
    ImageViewer { id: imageViewer; session: root.session }

    ReactionPicker { id: reactionPicker; session: root.session }

    // Confirms an irreversible message delete. For one's own one-to-one message it
    // is removed at the recipient too (no trace); otherwise it is removed locally.
    Dialog {
        id: deleteMessageDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        readonly property bool forEveryone: root.pendingDeleteOutgoing && root.session
        footer: DialogButtons {
            acceptText: "Delete"
            danger: true
            onAccepted: deleteMessageDialog.accept()
            onRejected: deleteMessageDialog.reject()
        }
        onAccepted: {
            if (root.session && root.pendingDeleteId !== null) {
                root.session.deleteMessage(root.pendingDeleteId, root.pendingDeleteProtocol,
                    root.pendingDeleteOutgoing)
            }
            root.pendingDeleteId = null
        }
        onRejected: root.pendingDeleteId = null
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
        header: Label {
            text: "Delete message"
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        contentItem: Label {
            text: deleteMessageDialog.forEveryone
                ? "Delete this message for everyone? It is removed from the recipient too, "
                    + "with no trace. This cannot be undone."
                : "Delete this message from this device? This cannot be undone."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Conversation header.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 60
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.narrow ? 2 : 12
                anchors.rightMargin: 6
                spacing: 10
                // The only pane on a narrow window, so this is the way back.
                IconButton {
                    visible: root.narrow
                    iconName: "back"
                    onClicked: if (root.session) { root.session.closeConversation() }
                    // Everything this chat is covering: the account's other
                    // conversations and the other accounts, whose switcher is
                    // behind the list too.
                    UnreadBadge {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 2
                        count: (root.session ? root.session.unreadTotal : 0) + App.unreadElsewhere
                    }
                }
                Avatar { fingerprint: root.session ? root.session.activePeer : ""; size: 38; enlargeable: true }
                // The name alone: the identity behind it is one tap away, under
                // the info button, where it can be read and copied properly.
                Label {
                    text: root.session ? root.session.activePeerName : ""
                    color: Theme.text
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                IconButton { iconName: "search"; onClicked: searchPopup.openSearch() }
                IconButton { iconName: "call"; onClicked: root.callRequested() }
                IconButton { iconName: "info"; onClicked: root.contactInfoRequested() }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // The message list shows only a window of the conversation: the newest
        // page on open, with older messages paged in at the top and newer ones at
        // the bottom (after a search jump), so even a huge dialog stays cheap.
        Item {
            id: listPane
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Building a screenful of bubbles takes a moment, and doing it inside
            // the click that switched accounts made the click itself feel stuck.
            // The switch happens now; the bubbles are built on the next turn of
            // the loop, with this in their place until they are.
            property bool building: false
            readonly property string openChat:
                (root.session ? root.session.accountId : "") + "/"
                    + (root.session ? root.session.activePeer : "")
            onOpenChatChanged: { building = true; buildDelay.restart() }
            Timer {
                id: buildDelay
                interval: 1
                onTriggered: listPane.building = false
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: listPane.building
                visible: running
            }

            ListView {
                id: messages
                anchors.fill: parent
                clip: true
                visible: !listPane.building
                spacing: 6
                topMargin: 10
                bottomMargin: 10
                model: listPane.building ? null : (root.session ? root.session.conversation : null)
                delegate: MessageBubble {
                    session: root.session
                    // Highlighted by a search jump, or as part of the unread tail that
                    // flashes briefly when the chat opens at its first unread message.
                    highlighted: ListView.view
                        && (ListView.view.highlightId === model.msgId
                            || (ListView.view.unreadFlashFromId >= 0 && !model.outgoing
                                && model.msgId >= ListView.view.unreadFlashFromId))
                    onDeleteRequested: function(msgId, protocolId, outgoing) {
                        root.confirmDeleteMessage(msgId, protocolId, outgoing)
                    }
                    onReactRequested: function(protocolId) { reactionPicker.openFor(protocolId) }
                    onImageRequested: function(url, messageId, name) {
                        imageViewer.show(url, messageId, name)
                    }
                }

                // Section messages by calendar day and show a centered date
                // separator wherever the day changes (driven by the model's "day"
                // role; the header text is humanised to Today / Yesterday / a date).
                section.property: "day"
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    id: sectionRoot
                    required property string section
                    width: messages.width
                    height: 30
                    Rectangle {
                        anchors.centerIn: parent
                        height: 20
                        width: dayLabel.implicitWidth + 18
                        radius: 10
                        color: Theme.surface
                        border.color: Theme.border
                        Label {
                            id: dayLabel
                            anchors.centerIn: parent
                            text: root.formatDaySection(sectionRoot.section)
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }

                // Whether the view is pinned to the bottom and follows new messages;
                // only true at the real newest (not while viewing older history).
                property bool stickToBottom: true
                // Set while a programmatic scroll runs so it is not mistaken for the
                // user moving the view.
                property bool autoScrolling: false
                // Set while a paging load runs so the new rows do not trigger the
                // new-message autoscroll.
                property bool paging: false
                // Message id to flash after a search jump (-1 = none).
                property var highlightId: -1
                // When >= 0, every unread message (id >= this) is briefly highlighted
                // right after opening a conversation at its first unread message.
                property var unreadFlashFromId: -1

                function scrollToEnd() {
                    autoScrolling = true
                    positionViewAtEnd()
                    autoScrolling = false
                }

                // Reliably land on the newest message after a (re)load. A single
                // positionViewAtEnd is unreliable until the freshly created
                // delegates have been measured, so re-scroll for a few frames
                // until the bottom is actually reached.
                function pinToBottom() {
                    pinTimer.ticks = 0
                    scrollToEnd()
                    pinTimer.restart()
                }
                Timer {
                    id: pinTimer
                    interval: 16
                    repeat: true
                    property int ticks: 0
                    onTriggered: {
                        messages.scrollToEnd()
                        ticks += 1
                        if (messages.atYEnd || ticks >= 12) {
                            messages.stickToBottom = root.session ? root.session.atNewest : true
                            stop()
                        }
                    }
                }

                // Restore where the user left this conversation after the active
                // account changes under us: at the bottom (and keep following new
                // messages) if it was pinned there, otherwise anchored on the row
                // that was at the top. Anchoring by row index (not by a raw contentY)
                // forces the delegate to be built, so the view never lands blank.
                function restoreScroll() {
                    if (!root.session) {
                        autoScrolling = false
                        return
                    }
                    const st = root.session.scrollFor(root.session.activePeer)
                    forceLayout()  // build delegates now so positioning is reliable
                    if (!st.has || st.stick || st.anchor < 0) {
                        stickToBottom = true
                        pinToBottom()
                    } else {
                        stickToBottom = false
                        restoreToRow(st.anchor)
                        // Re-assert next frame once late delegates have measured.
                        Qt.callLater(function() { messages.restoreToRow(st.anchor) })
                    }
                    Qt.callLater(root.markVisibleRead)
                }
                function restoreToRow(idx) {
                    if (count === 0) {
                        return
                    }
                    autoScrolling = true
                    positionViewAtIndex(Math.min(count - 1, Math.max(0, idx)), ListView.Beginning)
                    autoScrolling = false
                }
                // The active account changed (the user switched local profiles): the
                // model is now the new account's conversation. Restore that
                // conversation's last scroll position instead of resetting to the top.
                onModelChanged: if (model) { autoScrolling = true; Qt.callLater(restoreScroll) }

                function loadOlder() {
                    paging = true
                    const k = root.session.loadOlderMessages()
                    if (k > 0) {
                        // Keep the user on the same message: the previously-top item
                        // is now at index k.
                        autoScrolling = true
                        positionViewAtIndex(k, ListView.Beginning)
                        autoScrolling = false
                    }
                    paging = false
                }

                function loadNewer() {
                    paging = true
                    root.session.loadNewerMessages()  // appended below; view stays put
                    paging = false
                }

                onContentYChanged: {
                    root.markVisibleRead()  // scrolling a message into view reads it
                    if (autoScrolling || !root.session) {
                        return
                    }
                    // Only a genuine user gesture (drag/flick/wheel) updates the
                    // follow flag and the saved position. Programmatic scrolls and
                    // the settle that happens while a model is swapped in must not, or
                    // they would wrongly record the view as "scrolled up" and break
                    // the restore on the next switch. The saved anchor is the row at
                    // the top of the view (-1 when pinned to the bottom).
                    if (moving || dragging || flicking) {
                        stickToBottom = atYEnd && root.session.atNewest
                        const anchor = stickToBottom
                            ? -1 : indexAt(width / 2, contentY + topMargin + 2)
                        root.session.saveScroll(root.session.activePeer, anchor, stickToBottom)
                    }
                    // Fetch the next page while there is still a screenful above
                    // the user rather than at the very edge: at the edge the list
                    // stops dead until the page lands, which is what a small page
                    // would otherwise be felt as.
                    if (!paging && contentY - originY < height && root.session.hasMoreOlder) {
                        loadOlder()
                    } else if (!paging && contentY + height > contentHeight - height
                            && !root.session.atNewest) {
                        loadNewer()
                    }
                }
                // Keep following the bottom while the last bubble's height settles.
                onContentHeightChanged: if (stickToBottom && !paging) { scrollToEnd() }

                Connections {
                    target: messages.model
                    // A genuine new message (not a paging batch): an own message
                    // always scrolls into view; an incoming one only when pinned.
                    function onRowsInserted() {
                        if (messages.paging) {
                            return
                        }
                        if (messages.model.lastMessageOutgoing()) {
                            messages.stickToBottom = true
                        }
                        if (messages.stickToBottom && root.session) {
                            // Keep the saved position in sync when our own send
                            // re-pins us to the bottom, so a switch restores there.
                            root.session.saveScroll(root.session.activePeer, -1, true)
                        }
                        if (messages.stickToBottom) {
                            Qt.callLater(messages.scrollToEnd)
                        }
                        // A new incoming message in the open, focused, bottom-pinned
                        // chat is read on arrival.
                        Qt.callLater(root.markVisibleRead)
                    }
                    // Switching/reloading a conversation: land on the newest, unless
                    // a search jump positioned the window back in history (handled by
                    // onScrollToMessage).
                    function onModelReset() {
                        if (root.session && root.session.atNewest) {
                            messages.stickToBottom = true
                            messages.pinToBottom()
                        } else {
                            messages.stickToBottom = false
                        }
                        // Opening a chat reads what is on screen.
                        Qt.callLater(root.markVisibleRead)
                    }
                }

                Component.onCompleted: { Qt.callLater(restoreScroll) }
            }

            // Jump-to-latest: shown whenever the view is not resting at the true
            // bottom (scrolled up, or viewing older history after a search jump).
            RoundButton {
                id: jumpButton
                // Shown only when scrolled up more than half a screen above the
                // content end, so it never overlaps the latest messages. Measured
                // by distance (the bottom can sit below the visible area, so atYEnd
                // is not a reliable gate); the binding tracks contentY so it stays
                // current as the user scrolls.
                visible: messages.count > 0
                    && (messages.contentHeight + messages.originY
                        - messages.contentY - messages.height) > messages.height / 2
                hoverEnabled: true
                // Semi-transparent at rest, fully opaque on hover.
                opacity: jumpButton.hovered ? 1.0 : 0.45
                Behavior on opacity { NumberAnimation { duration: 120 } }
                width: 40
                height: 40
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                onClicked: if (root.session) { root.session.jumpToLatest() }
                background: Rectangle {
                    radius: width / 2
                    color: Theme.surface
                    border.color: Theme.border
                }
                // Centred by hand: RoundButton's own padding pulls a small
                // content item off-centre in a 40px circle.
                padding: 0
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        name: "down"
                        color: Theme.accent
                        size: 18
                    }
                }
            }
        }

        Composer { session: root.session }
    }
}
