// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

ComboBox {
    id: root

    readonly property int kRowHeight: 32
    readonly property int kListHeight: 320

    font.pixelSize: Theme.fontBody
    implicitHeight: 36
    hoverEnabled: true
    readonly property int kTextEdge: 12
    readonly property int kChevronEdge: 34
    leftPadding: root.mirrored ? root.kChevronEdge : root.kTextEdge
    rightPadding: root.mirrored ? root.kTextEdge : root.kChevronEdge
    readonly property int kSlack: 4
    implicitWidth: root.widestText + root.kTextEdge + root.kChevronEdge + root.kSlack

    property int widestText: 0
    // Measured on demand: a binding that drives the ruler would depend on what
    // it writes.
    function measure() {
        let widest = 0
        for (let i = 0; i < root.count; ++i) {
            const entry = root.model[i]
            ruler.text = root.textRole.length > 0 ? entry[root.textRole] : entry
            widest = Math.max(widest, ruler.width)
        }
        root.widestText = Math.ceil(widest)
    }
    onModelChanged: root.measure()
    onFontChanged: root.measure()
    Component.onCompleted: root.measure()

    TextMetrics { id: ruler; font: root.font }

    background: Rectangle {
        radius: 8
        color: root.pressed ? Theme.border2 : (root.hovered ? Theme.surfaceAlt : Theme.surface)
        border.color: root.activeFocus ? Theme.accent : Theme.border
    }

    contentItem: Label {
        text: root.displayText
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: root.mirrored ? Text.AlignRight : Text.AlignLeft
        elide: Text.ElideRight
    }

    indicator: Icon {
        name: "chevron"
        color: Theme.textDim
        size: 16
        anchors.right: root.right
        anchors.rightMargin: 10
        anchors.verticalCenter: root.verticalCenter
    }

    delegate: ItemDelegate {
        id: entry
        required property var modelData
        required property int index
        width: root.width
        height: root.kRowHeight
        hoverEnabled: true
        contentItem: Label {
            text: root.textRole.length > 0 ? entry.modelData[root.textRole] : entry.modelData
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: root.mirrored ? Text.AlignRight : Text.AlignLeft
            leftPadding: 8
            rightPadding: 8
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: entry.hovered || root.currentIndex === entry.index
                ? Theme.surfaceAlt : "transparent"
        }
        onClicked: { root.currentIndex = entry.index; root.activated(entry.index); root.popup.close() }
    }

    popup: Popup {
        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(list.contentHeight + 8, root.kListHeight)
        padding: 4
        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: Theme.border
        }
        contentItem: ListView {
            id: list
            clip: true
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }
}
