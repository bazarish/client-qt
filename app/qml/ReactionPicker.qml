// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    bottomMargin: App.keyboardInset
    property var session: null
    property string target: ""
    modal: true
    focus: true
    parent: Overlay.overlay
    readonly property int kColumns: 6
    width: Math.min(parent ? parent.width - 24 : 400,
        kColumns * kChipSize + (kColumns - 1) * kChipSpacing + 2 * padding)
    padding: 12
    background: DialogFrame { }

    property real askedX: 0
    property real askedY: 0
    readonly property int kEdgeGap: 12

    function openAt(e2eId, sceneX, sceneY) {
        root.target = e2eId
        customField.text = ""
        root.askedX = sceneX
        root.askedY = sceneY
        // Left set by a tap into it last time, the window would hand the field its focus again.
        customField.focus = false
        root.open()
        root.placeInside()
    }

    // Placed again once open: before that the popup has no height to keep inside.
    function placeInside() {
        const area = root.parent
        if (!area) {
            return
        }
        root.x = Math.max(kEdgeGap, Math.min(root.askedX, area.width - root.width - kEdgeGap))
        root.y = Math.max(kEdgeGap, Math.min(root.askedY, area.height - root.height - kEdgeGap))
    }
    onHeightChanged: if (root.opened) { root.placeInside() }
    onWidthChanged: if (root.opened) { root.placeInside() }
    Connections {
        target: root.parent
        function onHeightChanged() { if (root.opened) { root.placeInside() } }
    }

    readonly property var common: root.session ? root.session.standardReactions : []
    readonly property var recent: root.session ? root.session.recentReactions : []
    readonly property int kChipSize: 36
    readonly property int kChipSpacing: 6

    function pick(emoji) {
        const e = ("" + emoji).trim()
        if (root.session && root.target.length > 0 && e.length > 0) {
            root.session.react(root.target, e)
        }
        Qt.callLater(root.close)
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label { text: qsTr("React"); color: Theme.green; font.weight: Font.DemiBold }
        Flow {
            Layout.fillWidth: true
            spacing: root.kChipSpacing
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
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.pick(modelData)
                    }
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: root.kChipSpacing
            visible: root.recent.length > 0
            Repeater {
                model: root.recent
                Rectangle {
                    id: recentChip
                    required property var modelData
                    width: recentLabel.implicitWidth + 12 > root.kChipSize
                        ? root.kChipSize * 2 + root.kChipSpacing : root.kChipSize
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
                        font.family: Theme.reactionFont(recentChip.modelData)
                        renderType: Text.NativeRendering
                    }
                    HoverHandler { id: recentHover; cursorShape: Qt.PointingHandCursor }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.pick(recentChip.modelData)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: customField
                Layout.fillWidth: true
                placeholderText: qsTr("Any unicode…")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                // The emoji font has no digits, so the field keeps the brand font and the engine's fallback.
                font.family: Theme.fontFamily
                renderType: Text.NativeRendering
                onAccepted: root.pick(customField.text)
                // The cap counts code points, as the protocol does, and the word a keyboard is still composing.
                onDisplayTextChanged: {
                    const held = []
                    for (const point of customField.displayText) {
                        held.push(point)
                    }
                    if (held.length > App.maxReactionChars) {
                        Qt.inputMethod.commit()
                        customField.text = held.slice(0, App.maxReactionChars).join("")
                    }
                }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: customField.activeFocus ? Theme.accent : Theme.border }
            }
            MenuButton {
                iconName: "smile"
                text: qsTr("React")
                enabled: customField.displayText.trim().length > 0
                onClicked: root.pick(customField.text)
            }
        }
    }
}
