// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    property string title: ""
    property string note: ""
    property string link: ""
    property bool hasBack: false
    property bool copied: false
    readonly property int kCopiedMs: 1500
    signal back()

    onOpened: root.copied = false

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: root.title
            showBack: root.hasBack
            onBackClicked: root.back()
            onCloseClicked: root.close()
        }

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
                    Layout.leftMargin: Theme.contentInset
                    Layout.rightMargin: Theme.contentInset
                    Layout.topMargin: Theme.contentInset
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
                    Layout.leftMargin: Theme.contentInset
                    Layout.rightMargin: Theme.contentInset
                    Layout.preferredHeight: 96
                    TextArea {
                        id: linkArea
                        readOnly: true
                        wrapMode: TextArea.WrapAnywhere
                        text: root.link
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        selectByMouse: true
                        background: Rectangle { radius: 8; color: Theme.control; border.color: Theme.controlEdge }
                    }
                }
                MenuButton {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.contentInset
                    Layout.rightMargin: Theme.contentInset
                    Layout.bottomMargin: Theme.contentInset
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
