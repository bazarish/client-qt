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
                    if (autoScrolling || !root.session) {
                        return
                    }
                    stickToBottom = atYEnd && root.session.atNewest
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
                        if (messages.stickToBottom) {
                            Qt.callLater(messages.scrollToEnd)
                        }
                    }
                    // Switching/reloading a conversation: show the latest, unless a
                    // search jump positioned the window back in history.
                    function onModelReset() {
                        messages.stickToBottom = root.session ? root.session.atNewest : true
                        if (messages.stickToBottom) {
                            Qt.callLater(messages.scrollToEnd)
                        }
                    }
                }

                Component.onCompleted: scrollToEnd()
            }

            // Jump-to-latest: shown whenever the view is not resting at the true
            // bottom (scrolled up, or viewing older history after a search jump).
            RoundButton {
                id: jumpButton
                // Hidden once the view rests at the very bottom (so it never
                // overlaps the latest messages); shown whenever scrolled up.
                visible: messages.count > 0 && !messages.atYEnd
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
