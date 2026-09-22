// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// What a button holds: the drawing that names the action and the label, centred
// as one pair. Meant as a Button's `contentItem`, so the words come from the
// button itself unless they are given here, and the colour is set once for both.
Item {
    id: root
    // Which shape to draw (see Icon.qml); empty leaves the label on its own.
    property string name: ""
    property string text: parent && parent.text !== undefined ? parent.text : ""
    property color color: Theme.text
    property int weight: Font.Normal
    // What the button offers its content. The label elides inside what the icon
    // leaves of it, so it is the words that give way, never the drawing.
    property real available: width
    readonly property int kGap: 8

    // From the parts, not from the row: the label's width is bound to what the
    // button offers, so asking the row how wide it wants to be would ask the
    // button a question that depends on the answer.
    implicitWidth: label.implicitWidth + (icon.visible ? icon.width + root.kGap : 0)
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
            size: Theme.iconInline
        }
        Label {
            id: label
            width: Math.min(implicitWidth,
                root.available - (icon.visible ? icon.width + root.kGap : 0))
            text: root.text
            color: root.color
            font.weight: root.weight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
