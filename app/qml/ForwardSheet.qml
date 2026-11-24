// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    bottomMargin: typing ? App.keyboardInset : 0
    property var session: null
    property string e2eId: ""
    property var localIds: []
    signal forwarded()

    function openFor(id) {
        root.e2eId = id
        root.localIds = []
        filter.text = ""
        root.open()
    }
    function openForMany(ids) {
        root.e2eId = ""
        root.localIds = ids
        filter.text = ""
        root.open()
    }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Forward to")
            onCloseClicked: root.close()
        }

        FormField {
            id: filter
            Layout.fillWidth: true
            Layout.leftMargin: Theme.contentInset
            Layout.rightMargin: Theme.contentInset
            Layout.topMargin: Theme.contentInset
            placeholder: qsTr("Search chats")
        }

        ListView {
            id: chats
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: contentHeight
            Layout.topMargin: 8
            Layout.bottomMargin: Theme.radius
            clip: true
            model: root.session ? root.session.chatList : null
            delegate: ItemDelegate {
                width: chats.width
                height: visible ? 56 : 0
                visible: filter.displayText.length === 0
                    || (model.name || "").toLowerCase().indexOf(filter.displayText.toLowerCase()) >= 0
                background: Rectangle {
                    color: parent.hovered ? Theme.surfaceAlt : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    SavedMark {
                        visible: model.saved
                        size: 34
                        iconSize: 18
                    }
                    Avatar { visible: !model.saved; fingerprint: model.fingerprint; size: 34 }
                    Fingerprint {
                        value: model.fingerprint
                        replacement: model.name && model.name.length > 0 ? model.name : ""
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                onClicked: {
                    // Taken first: closing tears down the delegate this handler runs in.
                    const sheet = root
                    const session = root.session
                    const message = root.e2eId
                    const many = root.localIds
                    const target = model.fingerprint
                    root.close()
                    if (many.length > 0) {
                        session.forwardMessages(many, target)
                    } else {
                        session.forwardMessage(message, target)
                    }
                    sheet.forwarded()
                }
            }
        }
    }
}
