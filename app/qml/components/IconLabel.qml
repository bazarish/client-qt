// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Item {
    id: root
    property string name: ""
    property string text: parent && parent.text !== undefined ? parent.text : ""
    property color color: Theme.text
    property int weight: Font.Normal
    property real available: width
    property real iconSize: Theme.iconInline
    readonly property int kGap: 8
    readonly property int kMaxLines: 2
    readonly property bool worded: root.text.length > 0

    // From the parts, not the row: the row's width depends on this one, a binding loop.
    implicitWidth: (root.worded ? label.implicitWidth : 0)
        + (icon.visible ? icon.width + (root.worded ? root.kGap : 0) : 0)
    implicitHeight: Math.max(label.implicitHeight, icon.visible ? icon.height : 0)

    Row {
        anchors.centerIn: parent
        spacing: root.kGap
        Icon {
            id: icon
            anchors.verticalCenter: parent.verticalCenter
            visible: root.name.length > 0
            name: root.name
            color: root.color
            size: root.iconSize
        }
        Label {
            id: label
            visible: root.worded
            width: Math.min(implicitWidth,
                root.available - (icon.visible ? icon.width + root.kGap : 0))
            text: root.text
            color: root.color
            font.weight: root.weight
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: root.kMaxLines
            elide: Text.ElideRight
        }
    }
}
