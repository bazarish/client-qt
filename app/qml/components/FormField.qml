import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

ColumnLayout {
    property alias label: lbl.text
    property alias text: field.text
    property alias placeholder: field.placeholderText
    property alias echoMode: field.echoMode
    property alias inputField: field
    spacing: 4
    Layout.fillWidth: true

    Text {
        id: lbl
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }
    TextField {
        id: field
        Layout.fillWidth: true
        color: Theme.text
        selectByMouse: true
        background: Rectangle {
            radius: 8
            color: Theme.surface
            border.color: field.activeFocus ? Theme.accent : Theme.border
        }
    }
}
