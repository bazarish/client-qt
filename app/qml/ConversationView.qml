import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null
    signal contactInfoRequested()
    signal callRequested()

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
            onCountChanged: positionViewAtEnd()
            Component.onCompleted: positionViewAtEnd()
        }

        Composer { session: root.session }
    }
}
