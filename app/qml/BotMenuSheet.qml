// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    property var session: null

    onOpened: Qt.inputMethod.hide()

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Bot menu")
            onCloseClicked: root.close()
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
