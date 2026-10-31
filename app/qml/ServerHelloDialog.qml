// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property string reason: ""
    property string message: ""
    property var links: []

    function show(reasonText, messageText, linkList) {
        reason = reasonText || ""
        message = messageText || ""
        links = linkList || []
        open()
    }

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    z: 1000
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, (Overlay.overlay ? Overlay.overlay.width : 520) - 32)
    height: Math.min(helloCol.implicitHeight + topPadding + bottomPadding,
        (Overlay.overlay ? Overlay.overlay.height : 600) - 32)
    padding: 18
    background: DialogFrame { destructive: true }

    function copyText(t) {
        App.copyText(t)
        window.showToast(qsTr("Copied"))
    }

    contentItem: Flickable {
        id: helloFlick
        contentWidth: width
        contentHeight: helloCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: helloCol
            width: helloFlick.width
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("This server needs registration")
                    color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                }
            }

            Label {
                visible: root.reason.length > 0
                text: root.reason
                color: Theme.warn
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }

            ScrollView {
                visible: root.message.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, msgArea.implicitHeight + 16)
                TextArea {
                    id: msgArea
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    text: root.message
                    color: Theme.text
                    selectByMouse: true
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                }
            }

            Label {
                visible: root.links.length > 0
                text: qsTr("Open one of these to register, then connect again:")
                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Repeater {
                model: root.links
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        text: modelData
                        color: Theme.text
                        selectByMouse: true
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    }
                    IconButton { iconName: "copy"; onClicked: root.copyText(modelData) }
                    Button {
                        focusPolicy: Theme.pressFocus
                        text: qsTr("Open")
                        leftPadding: 10
                        rightPadding: 10
                        onClicked: Qt.openUrlExternally(modelData)
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                        contentItem: IconLabel { name: "link"; color: Theme.accent }
                    }
                }
            }

            Button {
                focusPolicy: Theme.pressFocus
                Layout.fillWidth: true
                text: qsTr("Close")
                hoverEnabled: true
                onClicked: root.close()
                background: Rectangle { radius: 10; color: parent.hovered ? Theme.accentHover : Theme.accent }
                contentItem: IconLabel { name: "close"; color: Theme.accentText }
            }
        }
    }
}
