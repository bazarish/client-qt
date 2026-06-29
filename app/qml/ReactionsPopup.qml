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
    width: Math.min(parent ? parent.width - 40 : 460, 460)
    height: Math.min(parent ? parent.height - 60 : 460, 480)
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
    // Per-member fan-out outcome for our OWN group message (empty for others'); a
    // resend updates it via the same revision.
    readonly property var sentList: (root.session && root.target.length > 0
        && root.session.reactionsRevision >= 0) ? root.session.groupDelivery(root.target) : []

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
            // Colour emoji icons (Twemoji, native-rendered) + a count, so all three
            // tabs fit the narrow popup: an eye (viewers), a heart (reactions) and an
            // envelope (delivery).
            component TabIcon: TabButton {
                property string glyph: ""
                property int count: 0
                contentItem: RowLayout {
                    spacing: 5
                    Item { Layout.fillWidth: true }
                    Label {
                        text: glyph
                        font.family: Theme.emojiFontFamily
                        renderType: Text.NativeRendering
                        font.pixelSize: 17
                    }
                    Label { text: count; color: Theme.text; font.pixelSize: Theme.fontSmall }
                    Item { Layout.fillWidth: true }
                }
            }
            TabIcon { glyph: "👁"; count: root.viewerList.length }
            TabIcon { glyph: "❤️"; count: root.reactionList.length }
            TabIcon { glyph: "✉️"; count: root.sentList.length }
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

            // Delivery: per-member status of our own group message, the same
            // lifecycle as a one-to-one message - at your server (grey) -> at the
            // recipient's server (yellow) -> read (green, if they send receipts) -
            // with a per-member Resend (re-sends just that member's copy).
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        visible: root.sentList.length === 0
                        text: "No send record, or this is not your message."
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.sentList
                        delegate: RowLayout {
                            required property var modelData
                            width: ListView.view ? ListView.view.width : 0
                            height: 44
                            spacing: 8
                            Item { width: 6 }
                            Avatar { fingerprint: modelData.member; size: 26 }
                            Label {
                                text: modelData.name
                                color: Theme.text
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            // 1 grey (at your server), 2 yellow (recipient's server),
                            // 3 green (read), 4 red (failed), 5 retrying.
                            Label {
                                text: modelData.status === 3 ? "read"
                                    : modelData.status === 2 ? "delivered"
                                    : modelData.status === 5 ? (modelData.detail && modelData.detail.length > 0
                                        ? "retrying " + modelData.detail : "retrying…")
                                    : modelData.status === 4 ? "not sent" : "at your server"
                                color: modelData.status === 3 ? Theme.green
                                    : modelData.status === 2 ? Theme.warn
                                    : modelData.status === 5 ? Theme.warn
                                    : modelData.status === 4 ? Theme.danger : Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                            // Resend while still in flight (grey/retrying) or failed.
                            MenuButton {
                                visible: modelData.status === 1 || modelData.status === 4
                                    || modelData.status === 5
                                text: "Resend"
                                onClicked: root.session.resendGroupToMember(root.target, modelData.member)
                            }
                            Item { width: 6 }
                        }
                    }
                }
            }
        }
    }
}
