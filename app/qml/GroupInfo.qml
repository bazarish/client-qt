import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

// Group panel: photo, member list, admin add/remove, and leave. Opened from the
// conversation header's i for a group.
Popup {
    id: root
    property var session: null
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(parent ? parent.height - 40 : 600, 660)
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    property bool addMode: false
    property var selectedFps: []
    onOpened: {
        addMode = false
        selectedFps = []
        renameField.text = root.session ? root.session.peerName(root.session.activePeer) : ""
    }

    readonly property bool admin: root.session ? root.session.activeGroupAdmin : false

    function saveGroupName() {
        if (root.session) {
            root.session.setGroupName(renameField.text)
        }
    }

    // Any member may set the group photo; the picked image is compressed and
    // broadcast, and appears in the chat as a "set the group photo" message.
    FileDialog {
        id: photoDialog
        title: "Choose a group photo"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: if (root.session) { root.session.setGroupAvatar("" + selectedFile) }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label {
                text: root.session ? root.session.peerName(root.session.activePeer) : "Group"
                color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
                Layout.fillWidth: true; elide: Text.ElideRight
            }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Group photo + name. Changing either is an admin privilege; everyone
        // else sees the photo read-only (the name is in the title above).
        ColumnLayout {
            visible: !root.addMode
            Layout.fillWidth: true
            Layout.topMargin: 12
            spacing: 6
            Avatar {
                Layout.alignment: Qt.AlignHCenter
                fingerprint: root.session ? root.session.activePeer : ""
                size: 72
            }
            MenuButton {
                Layout.alignment: Qt.AlignHCenter
                visible: root.admin
                text: "Set group photo"
                onClicked: photoDialog.open()
            }
            // Admin-only rename.
            RowLayout {
                visible: root.admin
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                spacing: 8
                TextField {
                    id: renameField
                    Layout.fillWidth: true
                    placeholderText: "Group name"
                    color: Theme.text
                    placeholderTextColor: Theme.textDim
                    onAccepted: root.saveGroupName()
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: renameField.activeFocus ? Theme.accent : Theme.border }
                }
                MenuButton { text: "Rename"; onClicked: root.saveGroupName() }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            spacing: 8

            // --- Member list ---
            RowLayout {
                visible: !root.addMode
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: root.session ? ((root.session.activeGroupMembers.length + 1) + " members"
                        + (root.admin ? " · you are an admin" : "")) : ""
                    color: Theme.textDim
                }
                MenuButton { visible: root.admin; text: "＋ Add"; onClicked: { root.selectedFps = []; root.addMode = true } }
            }
            ListView {
                visible: !root.addMode
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.session ? root.session.activeGroupMembers : []
                delegate: RowLayout {
                    id: memberRow
                    required property var modelData
                    width: ListView.view.width
                    height: 50
                    spacing: 10
                    // The member's display info: a local contact name (bright) or
                    // the member's own account name (green) with the short
                    // fingerprint beneath it.
                    readonly property var info: root.session ? root.session.groupSenderInfo(memberRow.modelData) : null
                    Avatar { fingerprint: memberRow.modelData; size: 32 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: memberRow.info ? memberRow.info.name : memberRow.modelData
                            color: (memberRow.info && memberRow.info.isContact) ? Theme.text : Theme.green
                            elide: Text.ElideRight; Layout.fillWidth: true
                        }
                        Label {
                            visible: memberRow.info && memberRow.info.fpShort.length > 0
                            text: memberRow.info ? "(" + memberRow.info.fpShort + ")" : ""
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                    // Add a fellow member as a one-to-one contact (a direct,
                    // member-only request - other members never see it). Hidden
                    // once they are a contact (contactsRevision re-drives this).
                    MenuButton {
                        visible: root.session && root.session.contactsRevision >= 0
                            && !root.session.isContact(memberRow.modelData)
                        text: "Add"
                        onClicked: root.session.addContactFromGroup(memberRow.modelData)
                    }
                    MenuButton {
                        visible: root.admin
                        text: "Remove"
                        danger: true
                        onClicked: root.session.removeGroupMember(root.session.activePeer, memberRow.modelData)
                    }
                }
            }

            // --- Add members (admin) ---
            Label { visible: root.addMode; text: "Add contacts to the group:"; color: Theme.textDim }
            ListView {
                visible: root.addMode
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.session ? root.session.contacts : null
                delegate: CheckDelegate {
                    width: ListView.view.width
                    readonly property bool already: root.session
                        && root.session.activeGroupMembers.indexOf(model.fingerprint) >= 0
                    visible: !model.isGroup && !already
                    height: (model.isGroup || already) ? 0 : 46
                    text: root.session ? root.session.peerName(model.fingerprint) : model.fingerprint
                    checked: root.selectedFps.indexOf(model.fingerprint) >= 0
                    onToggled: {
                        var a = root.selectedFps.slice()
                        var i = a.indexOf(model.fingerprint)
                        if (checked && i < 0) a.push(model.fingerprint)
                        else if (!checked && i >= 0) a.splice(i, 1)
                        root.selectedFps = a
                    }
                }
            }
            RowLayout {
                visible: root.addMode
                Layout.fillWidth: true
                IconButton { text: "‹"; font.pixelSize: 26; onClicked: root.addMode = false }
                Item { Layout.fillWidth: true }
                MenuButton {
                    text: "Add " + (root.selectedFps.length > 0 ? "(" + root.selectedFps.length + ")" : "")
                    enabled: root.selectedFps.length > 0
                    onClicked: { root.session.addGroupMembers(root.session.activePeer, root.selectedFps); root.addMode = false }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        MenuButton {
            visible: !root.addMode
            Layout.fillWidth: true
            Layout.margins: 12
            text: "Leave group"
            danger: true
            onClicked: { root.session.leaveGroup(root.session.activePeer); root.close() }
        }
    }
}
