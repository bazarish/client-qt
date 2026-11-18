// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

ColumnLayout {
    id: root
    property string title
    property color titleColor: Theme.green
    property bool showBack: false
    property bool showClose: true
    default property alias tools: tools.data
    signal backClicked()
    signal closeClicked()

    spacing: 0
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: Theme.headerInset
        Layout.minimumHeight: Theme.headerRowHeight
        spacing: 8
        IconButton {
            visible: root.showBack
            iconName: "back"
            onClicked: root.backClicked()
        }
        Label {
            Layout.fillWidth: true
            text: root.title
            color: root.titleColor
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        RowLayout {
            id: tools
            spacing: 8
        }
        IconButton {
            visible: root.showClose
            iconName: "close"
            onClicked: root.closeClicked()
        }
    }
    Hairline { }
}
