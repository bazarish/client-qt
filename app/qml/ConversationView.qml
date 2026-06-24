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
    }
    FileDialog {
        id: resendPickDialog
        onAccepted: if (root.session) { root.session.sendFile(selectedFile) }
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
                IconButton { text: "📞"; visible: !parent.isGroup; onClicked: root.callRequested() }
                IconButton { text: "📹"; visible: !parent.isGroup; onClicked: root.videoCallRequested() }
                IconButton { text: "ⓘ"; onClicked: root.contactInfoRequested() }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ListView {
            id: messages
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            topMargin: 10
            bottomMargin: 10
            model: root.session ? root.session.conversation : null
            delegate: MessageBubble { session: root.session }

            // Whether the view is pinned to the bottom and should follow new
            // messages; cleared once the user scrolls up into history.
            property bool stickToBottom: true
            // Set while a programmatic scroll runs so it is not mistaken for the
            // user moving the view.
            property bool autoScrolling: false

            function scrollToEnd() {
                autoScrolling = true
                positionViewAtEnd()
                autoScrolling = false
            }

            // A manual scroll (drag, flick or wheel) decides whether we are still
            // pinned to the bottom.
            onContentYChanged: if (!autoScrolling) { stickToBottom = atYEnd }
            // Keep following the bottom while the last bubble's height settles.
            onContentHeightChanged: if (stickToBottom) { scrollToEnd() }

            Connections {
                target: messages.model
                // An own outgoing message always scrolls into view; an incoming
                // one only when the user had not scrolled up into history.
                function onRowsInserted() {
                    if (messages.model.lastMessageOutgoing()) {
                        messages.stickToBottom = true
                    }
                    if (messages.stickToBottom) {
                        Qt.callLater(messages.scrollToEnd)
                    }
                }
                // Switching or reloading a conversation always shows the latest.
                function onModelReset() {
                    messages.stickToBottom = true
                    Qt.callLater(messages.scrollToEnd)
                }
            }

            Component.onCompleted: scrollToEnd()
        }

        Composer { session: root.session }
    }
}
