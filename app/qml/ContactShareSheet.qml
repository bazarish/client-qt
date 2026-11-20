// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    property var session: null
    property string link: ""
    property bool copied: false

    onOpened: {
        copied = false
        link = (session && session.activePeer.length > 0)
            ? session.contactInvite(session.activePeer) : ""
    }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Share contact")
            showBack: root.returnTo !== null
            onBackClicked: root.goBack()
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
                    text: root.session
                        ? root.session.contactName(root.session.activePeer)
                        : ""
                    color: Theme.text
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                QrView {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.link.length > 0
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
                        placeholderText: qsTr("No address held for this contact yet")
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
                    enabled: root.link.length > 0
                    iconName: "copy"
                    text: root.copied ? qsTr("Copied") : qsTr("Copy contact link")
                    onClicked: {
                        linkArea.selectAll(); linkArea.copy(); linkArea.deselect()
                        root.copied = true
                        copiedTimer.restart()
                    }
                }
            }
        }
    }
    Timer { id: copiedTimer; interval: 1500; onTriggered: root.copied = false }
}
