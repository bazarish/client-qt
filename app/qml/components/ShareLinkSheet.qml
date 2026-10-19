// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// A link to hand over, as a QR code, as text and to the clipboard.
Popup {
    id: root
    property string title: ""
    property string note: ""
    property string link: ""
    property bool hasBack: false
    property bool copied: false
    readonly property int kCopiedMs: 1500
    signal back()

    // The window's overlay: declared inside a page, the sheet would size itself to that page's column.
    parent: Overlay.overlay
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
            IconButton { visible: root.hasBack; iconName: "back"; onClicked: root.back() }
            Label {
                text: root.title
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
                    text: root.note
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
