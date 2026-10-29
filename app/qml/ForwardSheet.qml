import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Where to pass a message on to. The chats this account already has, nothing
// else: forwarding is a convenience for content the user already holds, not a way
// to start a conversation.
Popup {
    id: root
    bottomMargin: activeFocus ? App.keyboardInset : 0
    property var session: null
    // The message being passed on, by the id the two ends know it as; or several, by local id.
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

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 80 : 520, 520)
    padding: 0

    background: DialogFrame { }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            Icon { name: "forward"; color: Theme.accent; size: 16 }
            Label {
                text: qsTr("Forward to")
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
            placeholder: qsTr("Search chats")
        }

        ListView {
            id: chats
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            Layout.bottomMargin: Theme.radius
            clip: true
            model: root.session ? root.session.chatList : null
            delegate: ItemDelegate {
                width: chats.width
                height: visible ? 56 : 0
                // The filter is on the name the user sees, which is the only thing
                // they can search a chat by.
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
