// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

TabButton {
    id: tab
    focusPolicy: Theme.pressFocus
    readonly property int kMinHeight: 34
    readonly property int kTextInset: 8
    readonly property int kMaxLines: 2
    implicitHeight: Math.max(kMinHeight, contentItem.implicitHeight + 2 * kTextInset)
    padding: 0
    leftPadding: kTextInset
    rightPadding: kTextInset

    contentItem: Text {
        text: tab.text
        color: tab.checked ? Theme.text : Theme.textDim
        font.pixelSize: Theme.fontSmall
        font.weight: tab.checked ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        maximumLineCount: tab.kMaxLines
        elide: Text.ElideRight
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.margins: 3
        radius: Theme.radiusSmall
        color: tab.checked ? Theme.surfaceAlt
            : tab.hovered ? Theme.hoverTint : "transparent"
        border.width: tab.checked ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
