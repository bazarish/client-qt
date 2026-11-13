// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    signal chosen(bool primary)

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(360, (Overlay.overlay ? Overlay.overlay.width : 360) - 2 * Theme.spacing)
    background: DialogFrame { }
    header: Label {
        text: qsTr("Device role")
        color: Theme.green
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: qsTr("Cancel")
        showReject: false
        onAccepted: root.close()
    }
    contentItem: ColumnLayout {
        spacing: 10
        ButtonRow {
            Layout.fillWidth: true
            MenuButton {
                Layout.fillWidth: true
                iconName: "key"
                text: qsTr("Primary")
                onClicked: { root.close(); root.chosen(true) }
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "devices"
                text: qsTr("Secondary")
                onClicked: { root.close(); root.chosen(false) }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            text: qsTr("A secondary device does not manage the account")
        }
    }
}
