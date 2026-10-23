// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish
import "Emoji.js" as Emoji

// Emoji for the message being written. It takes no focus: the field keeps its cursor and a
// phone its keyboard.
Popup {
    id: root
    signal picked(string emoji)

    readonly property int kColumns: 8
    readonly property int kRows: 5
    readonly property int kCell: 40
    readonly property int kGlyphPixels: 24
    readonly property int kTabPixels: 18
    property int group: 0

    focus: false
    modal: false
    padding: 8
    margins: Theme.spacing
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: DialogFrame { }

    component Glyph: Rectangle {
        id: glyph
        property string emoji
        property int pixels: root.kGlyphPixels
        property bool current: false
        signal chosen()
        width: root.kCell
        height: root.kCell
        radius: Theme.radiusSmall
        color: glyphHover.hovered || glyph.current ? Theme.surfaceAlt : "transparent"
        Label {
            anchors.centerIn: parent
            text: glyph.emoji
            font.pixelSize: glyph.pixels
            font.family: Theme.emojiFontFamily
            renderType: Text.NativeRendering
        }
        HoverHandler { id: glyphHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: glyph.chosen() }
    }

    contentItem: ColumnLayout {
        spacing: 6
        Row {
            Repeater {
                model: Emoji.kGroups
                Glyph {
                    required property var modelData
                    required property int index
                    emoji: modelData[0]
                    pixels: root.kTabPixels
                    current: index === root.group
                    onChosen: root.group = index
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
            model: Emoji.kGroups[root.group]
            ScrollBar.vertical: ScrollBar { }
            delegate: Glyph {
                required property string modelData
                emoji: modelData
                onChosen: root.picked(modelData)
            }
        }
    }
}
