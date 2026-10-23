import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 80 : 520, implicitHeight)
    padding: 0

    background: DialogFrame { }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            Icon { name: "list"; color: Theme.accent; size: 16 }
            Label {
                text: qsTr("Bot menu")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

        ListView {
            id: entries
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            implicitHeight: contentHeight
            clip: true
            model: root.session ? root.session.activeBotMenu : []
            delegate: ItemDelegate {
                required property var modelData
                width: entries.width
                height: 48
                background: Rectangle {
                    color: parent.hovered ? Theme.surfaceAlt : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    Label {
                        text: modelData.title
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "/" + modelData.command
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                }
                onClicked: {
                    // Closing tears this delegate down, so what the send needs is taken first.
                    const session = root.session
                    const command = "/" + modelData.command
                    root.close()
                    session.sendOffered(command)
                }
            }
        }
    }
}
