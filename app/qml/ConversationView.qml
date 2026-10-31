// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Item {
    id: root
    property var session: null
    property bool narrow: false
    signal contactInfoRequested()
    signal callRequested()
    readonly property bool savedChat: root.session !== null
        && root.session.isSavedChat(root.session.activePeer)
    readonly property bool lookingUp: root.session !== null && root.session.activeAliasLookup
    signal addByNameRequested(string alias)

    property var pendingDeleteId: null
    property string pendingDeleteProtocol: ""
    property bool pendingDeleteOutgoing: false

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
        window.showToast(copied ? qsTr("Copied to clipboard") : qsTr("There is no text among them."))
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

    function markVisibleRead() {
        if (!session || !visible || messages.count === 0) {
            return
        }
        if (Qt.application.state !== Qt.ApplicationActive) {
            return
        }
        var row = messages.indexAt(messages.width / 2, messages.contentY + messages.height - 2)
        if (row < 0) {
            row = messages.count - 1
        }
        session.markReadThroughRow(row)
    }
    Connections {
        target: Qt.application
        function onStateChanged() { root.markVisibleRead() }
    }

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
        function onScrollToUnread(firstUnreadId) {
            Qt.callLater(function() {
                messages.endPin()
                const r = messages.model ? messages.model.rowForId(firstUnreadId) : -1
                if (r >= 0) {
                    messages.stickToBottom = false
                    messages.autoScrolling = true
                    messages.positionViewAtIndex(r, ListView.Beginning)
                    messages.autoScrolling = false
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
    Timer { id: unreadFlashTimer; interval: 2500; onTriggered: messages.unreadFlashFromId = -1 }
    Timer {
        id: reactionFlashTimer
        interval: 1600
        onTriggered: if (root.session) { root.session.forgetReactionFlash() }
    }
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

    ImageViewer { id: imageViewer; session: root.session }

    ReactionPicker { id: reactionPicker; session: root.session }
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

        Rectangle {
            Layout.fillWidth: true
            enabled: !composer.pickingEmoji
            implicitHeight: 60
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.narrow ? 2 : 12
                anchors.rightMargin: 6
                spacing: 10
                IconButton {
                    visible: root.narrow
                    iconName: "back"
                    onClicked: if (root.session) { root.session.closeConversation() }
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
                IconButton {
                    readonly property string kind: root.session && root.session.contactsRevision >= 0
                        ? root.session.contactKind(root.session.activePeer) : ""
                    visible: !root.savedChat && !root.lookingUp && kind === "user"
                    iconName: "call"
                    onClicked: root.callRequested()
                }
                IconButton {
                    visible: !root.lookingUp
                    iconName: "info"
                    onClicked: root.contactInfoRequested()
                }
            }
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

        Item {
            id: listPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Qt hands a touch to the handlers beneath a popup too: the bubbles took it from the viewer.
            enabled: !imageViewer.visible && !composer.pickingEmoji

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
                property TextEdit selectingText: null
                ScrollBoost { flickable: messages }
                anchors.fill: parent
                clip: true
                visible: !listPane.building
                spacing: 6
                topMargin: 10
                readonly property int kEndGapInSpacings: 2
                bottomMargin: spacing * kEndGapInSpacings
                model: listPane.building ? null : (root.session ? root.session.conversation : null)
                // A bubble pooled before the model was let go comes back with its bindings left on the old rows.
                reuseItems: !listPane.building
                delegate: MessageBubble {
                    session: root.session
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

                property bool stickToBottom: true
                property bool autoScrolling: false
                property bool paging: false
                property var highlightId: -1
                property var unreadFlashFromId: -1
                property bool settled: true
                opacity: settled ? 1 : 0

                function scrollToEnd() {
                    autoScrolling = true
                    positionViewAtEnd()
                    contentY = Math.max(originY - topMargin, originY + contentHeight + bottomMargin - height)
                    autoScrolling = false
                }

                // positionViewAtEnd misses until freshly created delegates are measured.
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

                // By row, not by contentY: positioning on a row builds its delegate.
                function restoreScroll() {
                    if (!root.session) {
                        autoScrolling = false
                        settled = true
                        return
                    }
                    const st = root.session.scrollFor(root.session.activePeer)
                    forceLayout()
                    if (!st.has || st.stick || st.anchor < 0) {
                        stickToBottom = true
                        pinToBottom()
                    } else {
                        stickToBottom = false
                        restoreToRow(st.anchor)
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
                    endPin()
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
                    root.session.loadNewerMessages()
                    Qt.callLater(function() { messages.paging = false })
                }

                onContentYChanged: {
                    root.markVisibleRead()
                    if (autoScrolling || !root.session) {
                        return
                    }
                    if (moving || dragging || flicking) {
                        stickToBottom = atYEnd && root.session.atNewest
                        const anchor = stickToBottom
                            ? -1 : indexAt(width / 2, contentY + topMargin + 2)
                        root.session.saveScroll(root.session.activePeer, anchor, stickToBottom)
                    }
                    if (!paging && !pinTimer.running && contentY - originY < height
                            && root.session.hasMoreOlder) {
                        Qt.callLater(loadOlder)
                    } else if (!paging && contentY + height > contentHeight - height
                            && !root.session.atNewest) {
                        Qt.callLater(loadNewer)
                    }
                }
                // Follow the bottom as bubbles settle, but not mid-gesture: a drag past the end re-measures rows above.
                onContentHeightChanged: if (stickToBottom && !paging && !moving) { scrollToEnd() }
                onMovementEnded: if (stickToBottom && !paging) { scrollToEnd() }
                onHeightChanged: if (stickToBottom && !paging) { scrollToEnd() }

                Connections {
                    target: messages.model
                    function onRowsInserted() {
                        if (messages.paging) {
                            return
                        }
                        if (messages.model.lastMessageOutgoing()) {
                            messages.stickToBottom = true
                        }
                        if (messages.stickToBottom && root.session) {
                            root.session.saveScroll(root.session.activePeer, -1, true)
                        }
                        if (messages.stickToBottom) {
                            Qt.callLater(messages.scrollToEnd)
                        }
                        Qt.callLater(root.markVisibleRead)
                    }
                    function onModelReset() {
                        if (root.session && root.session.atNewest) {
                            messages.stickToBottom = true
                            messages.pinToBottom()
                        } else {
                            messages.stickToBottom = false
                        }
                        Qt.callLater(root.markVisibleRead)
                    }
                }

                Component.onCompleted: { Qt.callLater(restoreScroll) }
            }

            RoundButton {
                id: jumpButton
                focusPolicy: Theme.pressFocus
                // Measured by distance: the bottom can sit below the visible area, so atYEnd is unreliable.
                visible: messages.count > 0
                    && (messages.contentHeight + messages.originY
                        - messages.contentY - messages.height) > messages.height / 2
                hoverEnabled: true
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
                // RoundButton's own padding pulls a small content item off-centre.
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

    RowLayout {
        visible: root.retryPeer.length > 0 && root.retryPeer === (root.session ? root.session.activePeer : "")
        enabled: !composer.pickingEmoji
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
        id: composer
        session: root.session
        visible: !root.session || (root.session.activeGroupWrite && !root.lookingUp)
    }
    }

    SelectionTools {
        anchors.fill: parent
        visible: App.mobile
        field: messages.selectingText
        view: messages
        bounds: messages
        active: messages.selectingText !== null
        onDismissed: root.endTextSelection()
    }
}
