// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// The account's own server, offered as the link a new account connects with.
Popup {
    id: root
    property var session: null
    property bool copied: false
    readonly property string link: root.session ? root.session.serverLink : ""
    readonly property int kCopiedMs: 1500
    signal back()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 40 : 640, body.implicitHeight + 84)
    padding: 0

    background: DialogFrame { }

    onOpened: root.copied = false

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { iconName: "back"; onClicked: root.back() }
            Label {
                text: qsTr("Share server")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: body
                width: root.width
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 14
                    text: qsTr("A friend scans the code or pastes the link to connect to this server.")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                QrView {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.link
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.preferredHeight: 96
                    TextArea {
                        id: linkArea
                        readOnly: true
                        wrapMode: TextArea.WrapAnywhere
                        text: root.link
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        selectByMouse: true
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    }
                }
                MenuButton {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 14
                    iconName: "copy"
                    text: root.copied ? qsTr("Copied") : qsTr("Copy link")
                    onClicked: {
                        linkArea.selectAll(); linkArea.copy(); linkArea.deselect()
                        root.copied = true
                        copiedTimer.restart()
                    }
                }
            }
        }
    }
    Timer { id: copiedTimer; interval: root.kCopiedMs; onTriggered: root.copied = false }
}
