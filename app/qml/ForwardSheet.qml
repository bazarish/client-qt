import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Where to pass a message on to. The chats this account already has, nothing
// else: forwarding is a convenience for content the user already holds, not a way
// to start a conversation.
Popup {
    id: root
    property var session: null
    // The message being passed on, by the id the two ends know it as.
    property string e2eId: ""

    function openFor(id) {
        root.e2eId = id
        filter.text = ""
        root.open()
    }

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 80 : 520, 520)
    padding: 0

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            Icon { name: "forward"; color: Theme.accent; size: 16 }
            Label {
                text: "Forward to"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

        FormField {
            id: filter
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            placeholder: "Search chats"
        }

        ListView {
            id: chats
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true
            model: root.session ? root.session.chatList : null
            delegate: ItemDelegate {
                width: chats.width
                height: visible ? 56 : 0
                // The filter is on the name the user sees, which is the only thing
                // they can search a chat by.
                visible: filter.text.length === 0
                    || (model.name || "").toLowerCase().indexOf(filter.text.toLowerCase()) >= 0
                background: Rectangle {
                    color: parent.hovered ? Theme.surfaceAlt : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    // The saved chat is first in this list, and carries its own
                    // mark rather than a face.
                    Rectangle {
                        visible: model.saved
                        implicitWidth: 34
                        implicitHeight: 34
                        radius: width / 2
                        color: Theme.surfaceAlt
                        Icon {
                            anchors.centerIn: parent
                            name: "bookmark"
                            color: Theme.green
                            size: 18
                        }
                    }
                    Avatar { visible: !model.saved; fingerprint: model.fingerprint; size: 34 }
                    Label {
                        text: model.name && model.name.length > 0
                            ? model.name : model.fingerprint.substring(0, 12)
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                onClicked: {
                    // Everything this needs is taken first, because closing tears
                    // down the delegate this handler is running in - and then the
                    // sheet goes, before the send, so one left standing cannot
                    // invite the same message being passed on again.
                    const session = root.session
                    const message = root.e2eId
                    const target = model.fingerprint
                    root.close()
                    session.forwardMessage(message, target)
                }
            }
        }
    }
}
