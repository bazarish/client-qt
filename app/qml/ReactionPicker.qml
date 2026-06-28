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
    width: 300
    padding: 12
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    function openFor(protocolId) {
        root.target = protocolId
        customField.text = ""
        root.open()
    }

    // A curated set covering the common cases; any other emoji can be typed below.
    readonly property var common: ["👍", "❤️", "😂", "🎉", "🔥", "😮", "😢", "🙏",
        "👀", "✅", "💯", "🚀", "😡", "👏", "🤔", "🥳"]

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
                    width: 36; height: 36; radius: 8
                    color: emojiHover.hovered ? Theme.surfaceAlt : Theme.surface
                    border.color: emojiHover.hovered ? Theme.green : Theme.border
                    Label { anchors.centerIn: parent; text: modelData; font.pixelSize: 18 }
                    HoverHandler { id: emojiHover; cursorShape: Qt.PointingHandCursor }
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
                placeholderText: "Any emoji…"
                color: Theme.text
                placeholderTextColor: Theme.textDim
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
