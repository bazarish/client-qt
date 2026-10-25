// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish
import "Emoji.js" as Emoji

// Takes no focus, so the field keeps its cursor and a phone its keyboard.
Popup {
    id: root
    signal picked(string emoji)

    readonly property int kColumns: 8
    readonly property int kRows: 5
    readonly property int kCell: 40
    readonly property int kGlyphPixels: 24
    readonly property int kTabPixels: 18
    // What was picked last, newest first; it is tab 0, the groups follow it.
    property var recent: []
    property int group: 0
    onAboutToShow: root.group = root.recent.length > 0 ? 0 : 1

    padding: 8
    margins: Theme.spacing
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: DialogFrame { }

    component Glyph: Rectangle {
        id: glyph
        property string emoji
        property string iconName: ""
        property int pixels: root.kGlyphPixels
        property bool current: false
        signal chosen()
        width: root.kCell
        height: root.kCell
        radius: Theme.radiusSmall
        color: glyphHover.hovered || glyph.current ? Theme.surfaceAlt : "transparent"
        Label {
            anchors.centerIn: parent
            visible: glyph.iconName.length === 0
            text: glyph.emoji
            font.pixelSize: glyph.pixels
            renderType: Text.NativeRendering
        }
        Icon {
            anchors.centerIn: parent
            visible: glyph.iconName.length > 0
            name: glyph.iconName
            size: glyph.pixels
        }
        HoverHandler { id: glyphHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: glyph.chosen() }
    }

    contentItem: ColumnLayout {
        spacing: 6
        Row {
            Glyph {
                iconName: "clock"
                pixels: root.kTabPixels
                current: root.group === 0
                onChosen: root.group = 0
            }
            Repeater {
                model: Emoji.kGroups
                Glyph {
                    required property var modelData
                    required property int index
                    emoji: modelData[0]
                    pixels: root.kTabPixels
                    current: index + 1 === root.group
                    onChosen: root.group = index + 1
                }
            }
        }
        Hairline { Layout.fillWidth: true }
        GridView {
            id: grid
            Layout.preferredWidth: root.kColumns * root.kCell
            Layout.preferredHeight: root.kRows * root.kCell
            cellWidth: root.kCell
            cellHeight: root.kCell
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.group === 0 ? root.recent : Emoji.kGroups[root.group - 1]
            ScrollBar.vertical: ScrollBar { }
            delegate: Glyph {
                required property string modelData
                emoji: modelData
                onChosen: root.picked(modelData)
            }
        }
    }
}
