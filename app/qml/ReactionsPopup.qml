import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// The "who reacted / who viewed" detail for a group message, in two tabs:
// "Viewers (total)" and "Reactions (total)". A single shared instance is opened
// (openFor) with the target message's protocol id. Group-only: a 1:1 chat has a
// single peer, so this carries no information there.
Popup {
    id: root
    property var session: null
    property string target: ""
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 360
    height: Math.min(parent ? parent.height - 60 : 440, 460)
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    function openFor(protocolId) {
        root.target = protocolId
        tabs.currentIndex = 0
        root.open()
    }

    // Re-query whenever any reaction or read receipt changes.
    readonly property var reactionList: (root.session && root.target.length > 0
        && root.session.reactionsRevision >= 0) ? root.session.reactionDetails(root.target) : []
    readonly property var viewerList: (root.session && root.target.length > 0
        && root.session.reactionsRevision >= 0) ? root.session.viewers(root.target) : []

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Label { text: "Message details"; color: Theme.green; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            TabButton { text: "Viewers (" + root.viewerList.length + ")" }
            TabButton { text: "Reactions (" + root.reactionList.length + ")" }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // Viewers (only our own messages accrue viewers; read receipts come to
            // the author, so for someone else's message this is empty).
            Item {
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 32
                    visible: root.viewerList.length === 0
                    text: "No reads yet, or this is not your message."
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                ListView {
                    anchors.fill: parent
                    clip: true
                    model: root.viewerList
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width
                        height: 44
                        spacing: 10
                        Item { width: 6 }
                        Label { text: "👁"; font.pixelSize: 16; font.family: Theme.emojiFontFamily; renderType: Text.NativeRendering }
                        Label { text: modelData.name; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                }
            }

            // Reactions: who reacted with what.
            Item {
                Label {
                    anchors.centerIn: parent
                    visible: root.reactionList.length === 0
                    text: "No reactions yet."
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                ListView {
                    anchors.fill: parent
                    clip: true
                    model: root.reactionList
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width
                        height: 44
                        spacing: 10
                        Item { width: 6 }
                        Label { text: modelData.emoji; font.pixelSize: 18; font.family: Theme.emojiFontFamily; renderType: Text.NativeRendering }
                        Label { text: modelData.name; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                }
            }
        }
    }
}
