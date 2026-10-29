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
    // The chat with ourselves: no face, no call, and its own panel behind the i.
    readonly property bool savedChat: root.session !== null
        && root.session.isSavedChat(root.session.activePeer)
    readonly property bool lookingUp: root.session !== null && root.session.activeAliasLookup
    // An alias tapped in a message: the window offers the add-a-contact form.
    signal addByNameRequested(string alias)

    // The message awaiting delete confirmation (set when a bubble asks to delete).
    property var pendingDeleteId: null
    property string pendingDeleteProtocol: ""
    property bool pendingDeleteOutgoing: false

    // Humanises a "yyyy-MM-dd" day key into a date-separator label.
    function formatDaySection(iso, language) {
        if (!iso || iso.length < 10) {
            return ""
        }
        const parts = iso.split("-")
        const d = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]))
        const now = new Date()
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const diff = Math.round((today.getTime() - d.getTime()) / 86400000)
        if (diff === 0) {
            return qsTr("Today")
        }
        if (diff === 1) {
            return qsTr("Yesterday")
        }
        return d.toLocaleDateString(Qt.locale(language), "d MMMM yyyy")
    }

    // Messages chosen for one action together, by local id; each value says whether it is ours.
    property var chosen: ({})
    readonly property int chosenCount: Object.keys(root.chosen).length
    readonly property int chosenOwn: Object.values(root.chosen).filter(own => own).length
    readonly property bool choosing: root.chosenCount > 0
    function toggleChosen(msgId, outgoing) {
        const next = Object.assign({}, root.chosen)
        if (next[msgId] !== undefined) {
            delete next[msgId]
        } else {
            next[msgId] = outgoing
        }
        root.chosen = next
    }
    function clearChosen() {
        root.chosen = ({})
    }
    function chosenIds() {
        return Object.keys(root.chosen).map(Number)
    }
    function copyChosen() {
        const copied = root.session.copyMessages(root.chosenIds()) > 0
        if (typeof window !== "undefined") {
            window.showToast(copied ? qsTr("Copied to clipboard") : qsTr("There is no text among them."))
        }
        if (copied) {
            root.clearChosen()
        }
    }
    Connections {
        target: root.session
        function onActivePeerChanged() { root.clearChosen() }
    }
    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: root.choosing
        onActivated: root.clearChosen()
    }

    function confirmDeleteMessage(msgId, e2eId, outgoing) {
        root.pendingDeleteId = msgId
        root.pendingDeleteProtocol = e2eId
        root.pendingDeleteOutgoing = outgoing
        deleteMessageDialog.open()
    }

    // A message already on screen is only lit; one off screen is brought to the middle first.
    function showMessage(row, localId) {
        const item = messages.itemAtIndex(row)
        const inSight = item !== null && item.y >= messages.contentY
            && item.y + item.height <= messages.contentY + messages.height
        if (!inSight) {
            messages.endPin()
            messages.stickToBottom = false
            messages.autoScrolling = true
            messages.positionViewAtIndex(row, ListView.Center)
            messages.autoScrolling = false
        }
        messages.highlightId = localId
        highlightTimer.restart()
    }
    function endTextSelection() {
        if (messages.selectingText) {
            messages.selectingText.deselect()
        }
        messages.selectingText = null
    }
    function showQuoted(localId) {
        const row = messages.model ? messages.model.rowForId(localId) : -1
        if (row >= 0) {
            showMessage(row, localId)
        } else if (session) {
            session.openConversationAtMessage(session.activePeer, localId)
        }
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
    // The automatic tries of a refused contact request are spent: offer to send
    // it again by hand, in the chat the user is already looking at.
    property string retryPeer: ""
    Connections {
        target: root.session
        function onContactRetryExhausted(fingerprint) { root.retryPeer = fingerprint }
        function onResendFilePickRequested() { resendPickDialog.open() }
        function onScrollToMessage(localId) {
            Qt.callLater(function() {
                const r = messages.model ? messages.model.rowForId(localId) : -1
                if (r >= 0) {
                    root.showMessage(r, localId)
                }
            })
        }
        // Open-at-first-unread: land on the first unread message (near the top) and
        // briefly highlight the unread tail, then read what is on screen.
        function onScrollToUnread(firstUnreadId) {
            Qt.callLater(function() {
                messages.endPin()
                const r = messages.model ? messages.model.rowForId(firstUnreadId) : -1
                if (r >= 0) {
                    messages.stickToBottom = false
                    messages.autoScrolling = true
                    messages.positionViewAtIndex(r, ListView.Beginning)
                    messages.autoScrolling = false
                    // An unread tail that fits opens at the bottom, which then follows a keyboard and new messages.
                    const first = messages.itemAtIndex(r)
                    if (first !== null && root.session.atNewest
                            && messages.originY + messages.contentHeight - first.y <= messages.height) {
                        messages.stickToBottom = true
                        messages.pinToBottom()
                    }
                    messages.unreadFlashFromId = firstUnreadId
                    unreadFlashTimer.restart()
                }
                Qt.callLater(root.markVisibleRead)
            })
        }
        // Pictures decode after their bubble is built and grow it: the jump keeps following them.
        function onScrollToBottom() {
            messages.stickToBottom = true
            root.session.saveScroll(root.session.activePeer, -1, true)
            messages.pinToBottom()
        }
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
    // Reactions that arrived while nobody was looking flash once when the chat is
    // opened, then stop being new. The wait is what the flash lasts plus a moment
    // to be seen at all: forgetting them re-reads the chips, and doing it too
    // early would take the colour away mid-animation.
    Timer {
        id: reactionFlashTimer
        interval: 1600
        onTriggered: if (root.session) { root.session.forgetReactionFlash() }
    }
    // Whenever this chat becomes the open one, and whenever a reaction lands in
    // it, anything pending gets its flash and the clock above starts.
    Connections {
        target: root.session
        function onActivePeerChanged() { reactionFlashTimer.restart() }
        function onReactionsRevisionChanged() {
            if (!reactionFlashTimer.running) {
                reactionFlashTimer.restart()
            }
        }
    }

    SearchPopup {
        id: searchPopup
        session: root.session
        anchors.centerIn: Overlay.overlay
        onJumpRequested: function(localId) {
            close()
            if (root.session) {
                root.session.openConversationAtMessage(root.session.activePeer, localId)
            }
        }
    }

    // Single shared reaction picker, opened by any bubble with its message's
    // protocol id (so the chat pays no popup per row).
    ImageViewer { id: imageViewer; session: root.session }

    ReactionPicker { id: reactionPicker; session: root.session }
    // One warning for the whole conversation: every web address in it is followed
    // through here.
    LinkWarningDialog { id: linkWarning }
    Dialog {
        id: groupActionDialog
        property string command: ""
        property string postId: ""
        property string author: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        footer: DialogButtons {
            acceptText: groupActionDialog.command === "exclude" ? qsTr("Exclude")
                : groupActionDialog.command === "block" ? qsTr("Block") : qsTr("Delete")
            danger: true
            onAccepted: groupActionDialog.accept()
            onRejected: groupActionDialog.reject()
        }
        onAccepted: {
            if (root.session) {
                root.session.sendCommand(groupActionDialog.command, groupActionDialog.postId,
                    groupActionDialog.command === "exclude"
                        ? qsTr("Exclude %1").arg(groupActionDialog.author)
                        : groupActionDialog.command === "block"
                            ? qsTr("Block %1").arg(groupActionDialog.author)
                            : qsTr("Delete for everyone"))
            }
        }
        background: DialogFrame { destructive: true }
        header: Label {
            text: groupActionDialog.command === "exclude"
                ? qsTr("Exclude %1?").arg(groupActionDialog.author)
                : groupActionDialog.command === "block"
                    ? qsTr("Block %1?").arg(groupActionDialog.author)
                    : qsTr("Delete for everyone?")
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        contentItem: Label {
            text: groupActionDialog.command === "exclude"
                ? qsTr("They can join again.")
                : groupActionDialog.command === "block"
                    ? qsTr("They cannot come back.")
                    : qsTr("The post is removed for every member.")
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
    ForwardSheet { id: forwardSheet; session: root.session; onForwarded: root.clearChosen() }

    Dialog {
        id: deleteChosenDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        // In a group every message goes from this device only.
        readonly property int own: root.session && !root.session.activeIsGroup ? root.chosenOwn : 0
        footer: DialogButtons {
            acceptText: qsTr("Delete")
            danger: true
            onAccepted: deleteChosenDialog.accept()
            onRejected: deleteChosenDialog.reject()
        }
        onAccepted: {
            if (root.session) {
                root.session.deleteMessages(root.chosenIds())
            }
            root.clearChosen()
        }
        background: DialogFrame { destructive: true }
        header: Label {
            text: qsTr("Delete selected messages?")
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        contentItem: Label {
            text: root.savedChat ? qsTr("They are removed from your other devices too.")
                : deleteChosenDialog.own === 0 ? qsTr("They are removed from this device only.")
                : deleteChosenDialog.own === root.chosenCount
                ? qsTr("They are removed from the other side too.")
                : qsTr("Your messages among them (%1) are removed from the other side too, the rest only from this device.").arg(deleteChosenDialog.own)
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
    BotMenuSheet { id: botMenuSheet; session: root.session }
    // Confirms an irreversible message delete. For one's own one-to-one message it
    // is removed at the recipient too (no trace); otherwise it is removed locally.
    Dialog {
        id: deleteMessageDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        readonly property bool forEveryone: root.pendingDeleteOutgoing && root.session
        footer: DialogButtons {
            acceptText: qsTr("Delete")
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
        background: DialogFrame { destructive: true }
        header: Label {
            text: qsTr("Delete message")
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        contentItem: Label {
            text: root.savedChat
                ? qsTr("Delete this message? It is removed from your other devices too.")
                : deleteMessageDialog.forEveryone
                ? qsTr("Delete this message for everyone? It is removed from the recipient too, with no trace.")
                : qsTr("Delete this message from this device? This cannot be undone.")
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
                SavedMark {
                    visible: root.savedChat
                    size: 38
                    iconSize: 20
                }
                Avatar {
                    visible: !root.savedChat
                    fingerprint: root.session ? root.session.activePeer : ""
                    size: 38
                    enlargeable: true
                }
                // The name alone: the identity behind it is one tap away, under
                // the info button, where it can be read and copied properly.
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Label {
                        text: root.session ? root.session.activePeerName : ""
                        color: Theme.text
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: root.session !== null && root.session.activeIsGroup
                        text: root.session && root.session.activeIsChannel
                            ? qsTr("Subscribers: %1").arg(root.session.activeGroupMembers)
                            : qsTr("Members: %1").arg(root.session ? root.session.activeGroupMembers : 0)
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        HoverHandler { id: countHover }
                        ToolTip.visible: countHover.hovered
                        ToolTip.text: qsTr("Updated from time to time; the server sets the count.")
                    }
                    Label {
                        readonly property string peer: root.session ? root.session.activePeer : ""
                        readonly property string kind: root.session && peer.length > 0
                            ? root.session.contactKind(peer) : ""
                        readonly property bool typing: root.session !== null && root.session.presenceRevision >= 0
                            && root.session.peerTyping(peer)
                        readonly property bool online: root.session !== null && root.session.presenceRevision >= 0
                            && root.session.peerOnline(peer)
                        visible: root.session !== null && !root.session.activeIsGroup && !root.savedChat
                            && (kind === "bot" || online)
                        text: kind === "bot" ? qsTr("bot") : (typing ? qsTr("typing…") : qsTr("online"))
                        color: kind === "bot" ? Theme.textFaint : Theme.green
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                IconButton {
                    visible: !root.lookingUp
                    iconName: "search"
                    onClicked: searchPopup.openSearch()
                }
                // There is nobody to call in the saved chat or in a group.
                IconButton {
                    visible: !root.savedChat && !root.lookingUp
                        && !(root.session && root.session.activeIsGroup)
                    iconName: "call"
                    onClicked: root.callRequested()
                }
                IconButton {
                    visible: !root.lookingUp
                    iconName: "info"
                    onClicked: root.contactInfoRequested()
                }
            }
            // While messages are chosen, what can be done with them takes the header's place.
            Rectangle {
                anchors.fill: parent
                visible: root.choosing
                color: Theme.surface
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: root.narrow ? 2 : 12
                    anchors.rightMargin: 6
                    spacing: 10
                    IconButton {
                        iconName: "close"
                        onClicked: root.clearChosen()
                    }
                    Label {
                        text: qsTr("Selected: %1").arg(root.chosenCount)
                        color: Theme.text
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    IconButton {
                        iconName: "copy"
                        onClicked: root.copyChosen()
                    }
                    IconButton {
                        iconName: "forward"
                        onClicked: forwardSheet.openForMany(root.chosenIds())
                    }
                    IconButton {
                        iconName: "trash"
                        onClicked: deleteChosenDialog.open()
                    }
                }
            }
        }
        Hairline { }

        RowLayout {
            id: botMenuStrip
            visible: root.session !== null && root.session.activeBotMenu.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 6
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 8
            Label {
                text: qsTr("Bot menu")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            MenuButton {
                text: qsTr("Open")
                onClicked: botMenuSheet.open()
            }
            TapHandler { onTapped: botMenuSheet.open() }
        }
        Hairline { visible: botMenuStrip.visible }

        // The message list shows only a window of the conversation: the newest
        // page on open, with older messages paged in at the top and newer ones at
        // the bottom (after a search jump), so even a huge dialog stays cheap.
        Item {
            id: listPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Qt hands a touch to the handlers beneath a popup too: the bubbles took it from the viewer.
            enabled: !imageViewer.visible

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
                // A bubble's text a phone is selecting, null when none.
                property TextEdit selectingText: null
                ScrollBoost { flickable: messages }
                anchors.fill: parent
                clip: true
                visible: !listPane.building
                spacing: 6
                topMargin: 10
                // Twice the gap between bubbles: the end of the chat reads as the end.
                readonly property int kEndGapInSpacings: 2
                bottomMargin: spacing * kEndGapInSpacings
                model: listPane.building ? null : (root.session ? root.session.conversation : null)
                reuseItems: true
                delegate: MessageBubble {
                    session: root.session
                    // Highlighted by a search jump, or as part of the unread tail that
                    // flashes briefly when the chat opens at its first unread message.
                    highlighted: ListView.view
                        && (ListView.view.highlightId === model.msgId
                            || (ListView.view.unreadFlashFromId >= 0 && !model.outgoing
                                && model.msgId >= ListView.view.unreadFlashFromId))
                    onDeleteRequested: function(msgId, e2eId, outgoing) {
                        root.confirmDeleteMessage(msgId, e2eId, outgoing)
                    }
                    choosing: root.choosing
                    chosen: root.chosen[model.msgId] !== undefined
                    onChosenToggled: function(msgId, outgoing) { root.toggleChosen(msgId, outgoing) }
                    onGroupActionRequested: function(command, postId, author) {
                        groupActionDialog.command = command
                        groupActionDialog.postId = postId
                        groupActionDialog.author = author
                        groupActionDialog.open()
                    }
                    onReactRequested: function(e2eId, sceneX, sceneY) {
                        reactionPicker.openAt(e2eId, sceneX, sceneY)
                    }
                    onForwardRequested: function(e2eId) { forwardSheet.openFor(e2eId) }
                    onQuoteRequested: function(localId) { root.showQuoted(localId) }
                    onSelectTextRequested: function(text) {
                        messages.selectingText = text
                        text.selectAll()
                    }
                    onImageRequested: function(url, localId, name) {
                        imageViewer.show(url, localId, name)
                    }
                    onLinkRequested: function(url) { linkWarning.ask(url) }
                    onAliasRequested: function(alias) { root.addByNameRequested(alias) }
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
                            text: root.formatDaySection(sectionRoot.section, Tr.language)
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
                // A new page is drawn only where it ends up: an opened chat showed the top of
                // its page, then jumped to the end.
                property bool settled: true
                opacity: settled ? 1 : 0

                function scrollToEnd() {
                    autoScrolling = true
                    positionViewAtEnd()
                    // That puts the last row on the edge, leaving out the bottom margin a rebound shows.
                    contentY = Math.max(originY - topMargin, originY + contentHeight + bottomMargin - height)
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
                function endPin() {
                    pinTimer.stop()
                    settled = true
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
                            messages.endPin()
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
                        settled = true
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
                        Qt.callLater(function() {
                            messages.restoreToRow(st.anchor)
                            messages.settled = true
                        })
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
                // The active account changed (the user switched local accounts): the
                // model is now the new account's conversation. Restore that
                // conversation's last scroll position instead of resetting to the top.
                onModelChanged: {
                    // Qt makes row 0 current; after a prepended page that item kept the boundary row's bubble.
                    currentIndex = -1
                    if (model) {
                        settled = false
                        autoScrolling = true
                        Qt.callLater(restoreScroll)
                    }
                }

                function loadOlder() {
                    if (paging || !root.session) {
                        return
                    }
                    paging = true
                    // A pin-to-bottom still ticking is a scrollToEnd on a timer,
                    // and reading older history says it is no longer wanted.
                    endPin()
                    // Reading older history is also proof the view is not following
                    // the bottom. Left set, the follow rule fires while the page
                    // lands and throws the user back to the newest message.
                    stickToBottom = false
                    // The view keeps the rows on screen in place itself; setting contentY would stop a fling.
                    root.session.loadOlderMessages()
                    Qt.callLater(function() { messages.paging = false })
                }

                function loadNewer() {
                    if (paging) {
                        return
                    }
                    paging = true
                    root.session.loadNewerMessages()  // appended below; view stays put
                    Qt.callLater(function() { messages.paging = false })
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
                // Follow the bottom as bubbles settle, but not mid-gesture: a drag past the end re-measures rows above.
                onContentHeightChanged: if (stickToBottom && !paging && !moving) { scrollToEnd() }
                onMovementEnded: if (stickToBottom && !paging) { scrollToEnd() }
                // A phone's keyboard shortens the list from below: the newest stays in sight.
                onHeightChanged: if (stickToBottom && !paging) { scrollToEnd() }

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
                focusPolicy: Theme.pressFocus
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

        // Shown only after the automatic tries are spent; sending again clears it.
    RowLayout {
        visible: root.retryPeer.length > 0 && root.retryPeer === (root.session ? root.session.activePeer : "")
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Layout.bottomMargin: 4
        spacing: 8
        Label {
            text: qsTr("The request was refused - their server is busy.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        MenuButton {
            iconName: "send"
            text: qsTr("Send the request again")
            onClicked: {
                root.session.retryContactRequest(root.retryPeer)
                root.retryPeer = ""
            }
        }
    }

    Composer {
        session: root.session
        visible: !root.session || (root.session.activeGroupWrite && !root.lookingUp)
    }
    }

    SelectionTools {
        id: bubbleSelection
        anchors.fill: parent
        visible: App.mobile
        field: messages.selectingText
        view: messages
        bounds: messages
        active: messages.selectingText !== null
        onDismissed: root.endTextSelection()
    }
}
