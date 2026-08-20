import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// A small emoji picker for reacting to a message: a grid of common emoji plus a
// field for any other emoji. A single shared instance is opened (openFor) with
// the target message's protocol id, so the chat does not pay a popup per bubble.
Popup {
    id: root
    property var session: null
    property string target: ""
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(300, parent ? parent.width - 24 : 300)
    padding: 12
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    function openFor(protocolId) {
        root.target = protocolId
        customField.text = ""
        root.open()
    }

    // The set comes from the session, which also decides what counts as one of
    // the user's own - the two must not drift apart.
    readonly property var common: root.session ? root.session.standardReactions : []
    // What this user reached for that is not in that set, newest first.
    readonly property var recent: root.session ? root.session.recentReactions : []
    // Matches the protocol's cap; the field cannot hold more, and a longer one
    // would be dropped by the other side anyway.
    readonly property int kMaxChars: 4
    // The grid unit the chips are built from.
    readonly property int kChipSize: 36

    function pick(emoji) {
        const e = ("" + emoji).trim()
        if (root.session && root.target.length > 0 && e.length > 0) {
            root.session.react(root.target, e)
        }
        root.close()
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label { text: "React"; color: Theme.green; font.weight: Font.DemiBold }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: root.common
                Rectangle {
                    required property var modelData
                    width: root.kChipSize; height: root.kChipSize; radius: 8
                    color: emojiHover.hovered ? Theme.surfaceAlt : Theme.surface
                    border.color: emojiHover.hovered ? Theme.green : Theme.border
                    Label {
                        anchors.centerIn: parent
                        text: modelData
                        color: Theme.text
                        font.pixelSize: 18
                        font.family: Theme.emojiFontFamily
                        renderType: Text.NativeRendering
                    }
                    HoverHandler { id: emojiHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.pick(modelData) }
                }
            }
        }
        // Reactions this user has used before that are not in the set above.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: root.recent.length > 0
            Repeater {
                model: root.recent
                Rectangle {
                    id: recentChip
                    required property var modelData
                    // A saved reaction can be four characters of plain text, which
                    // does not fit the square an emoji sits in. Two sizes only, so
                    // the row still reads as a grid: one square, or two.
                    width: recentLabel.implicitWidth + 12 > root.kChipSize
                        ? root.kChipSize * 2 : root.kChipSize
                    height: root.kChipSize
                    radius: 8
                    clip: true
                    color: recentHover.hovered ? Theme.surfaceAlt : Theme.surface
                    border.color: recentHover.hovered ? Theme.green : Theme.border
                    Label {
                        id: recentLabel
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, recentChip.width - 8)
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: recentChip.modelData
                        color: Theme.text
                        font.pixelSize: 18
                        font.family: Theme.emojiFontFamily
                        renderType: Text.NativeRendering
                    }
                    HoverHandler { id: recentHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.pick(modelData) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: customField
                Layout.fillWidth: true
                maximumLength: root.kMaxChars
                placeholderText: "Any unicode…"
                color: Theme.text
                placeholderTextColor: Theme.textDim
                // Show the typed emoji in colour too.
                font.family: Theme.emojiFontFamily
                renderType: Text.NativeRendering
                onAccepted: root.pick(customField.text)
                background: Rectangle { radius: 8; color: Theme.surface; border.color: customField.activeFocus ? Theme.accent : Theme.border }
            }
            MenuButton {
                text: "React"
                enabled: customField.text.trim().length > 0
                onClicked: root.pick(customField.text)
            }
        }
    }
}
