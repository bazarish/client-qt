// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// A chooser pill that shows a drawing for its words; the words stay its name.
ModeTab {
    id: tab
    property string iconName: ""
    readonly property int kMinWidth: 44
    readonly property int kIconSize: 18

    implicitWidth: Math.max(kMinWidth, kIconSize + leftPadding + rightPadding)
    Accessible.name: tab.text

    contentItem: Item {
        implicitWidth: tab.kIconSize
        implicitHeight: tab.kIconSize
        Icon {
            anchors.centerIn: parent
            name: tab.iconName
            size: tab.kIconSize
            color: tab.checked ? Theme.text : Theme.textDim
        }
    }
}
