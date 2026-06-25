import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Item {
    id: root
    property var session: null
    signal contactInfoRequested()
    signal callRequested()
    signal videoCallRequested()

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
        function onScrollToBottom() { messages.scrollToEnd() }
    }
    FileDialog {
        id: resendPickDialog
        onAccepted: if (root.session) { root.session.sendFile(selectedFile) }
    }
    Timer { id: highlightTimer; interval: 1800; onTriggered: messages.highlightId = -1 }

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
                anchors.leftMargin: 12
                anchors.rightMargin: 6
                spacing: 10
                readonly property bool isGroup: root.session && root.session.isGroup(root.session.activePeer)
                Avatar { fingerprint: root.session ? root.session.activePeer : ""; size: 38 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: root.session ? root.session.peerName(root.session.activePeer) : ""
                        color: Theme.text
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: parent.parent.isGroup ? "group · end-to-end encrypted" : "end-to-end encrypted"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                }
                IconButton { text: "🔍"; onClicked: searchPopup.openSearch() }
                IconButton { text: "📞"; visible: !parent.isGroup; onClicked: root.callRequested() }
                IconButton { text: "📹"; visible: !parent.isGroup; onClicked: root.videoCallRequested() }
                IconButton { text: "ⓘ"; onClicked: root.contactInfoRequested() }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // The message list shows only a window of the conversation: the newest
        // page on open, with older messages paged in at the top and newer ones at
        // the bottom (after a search jump), so even a huge dialog stays cheap.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: messages
                anchors.fill: parent
                clip: true
                spacing: 6
                topMargin: 10
                bottomMargin: 10
                model: root.session ? root.session.conversation : null
                delegate: MessageBubble {
                    session: root.session
                    highlighted: ListView.view && ListView.view.highlightId === model.msgId
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
                    if (atYBeginning && root.session.hasMoreOlder && !paging) {
                        loadOlder()
                    } else if (atYEnd && !root.session.atNewest && !paging) {
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
                text: "⌄"
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
                contentItem: Label {
                    text: "⌄"
                    color: Theme.accent
                    font.pixelSize: 20
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Composer { session: root.session }
    }
}
