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
        // else sees the photo read-only and who administers the group.
        ColumnLayout {
            visible: !root.addMode
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.bottomMargin: 4
            spacing: 8
            // Centred avatar (anchored in a full-width item so it never drifts to
            // the edge when the admin controls below are hidden).
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 88
                Avatar {
                    anchors.horizontalCenter: parent.horizontalCenter
                    fingerprint: root.session ? root.session.activePeer : ""
                    size: 88
                }
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
            // Non-admin: who administers the group (the rename/photo controls are
            // hidden for them, so this fills the space and explains the read-only).
            Label {
                readonly property string admins: (root.session && root.session.activeGroupMembers !== undefined)
                    ? root.session.groupAdminNames() : ""
                visible: !root.admin && admins.length > 0
                Layout.alignment: Qt.AlignHCenter
                text: "Admin: " + admins
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
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

            // You (the current user) - always a member, shown first with no actions.
            RowLayout {
                visible: !root.addMode
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                spacing: 8
                Avatar { fingerprint: root.session ? root.session.fingerprint : ""; size: 32 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: "You"; color: Theme.text; font.weight: Font.Medium }
                    Label {
                        visible: root.admin
                        text: "admin"
                        color: Theme.green; font.pixelSize: 10; font.weight: Font.Medium
                    }
                    Item { Layout.fillWidth: true }
                }
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
                    height: 52
                    spacing: 8
                    // The member's display info: a local contact name (bright) or
                    // the member's own account name (green) with the short
                    // fingerprint beneath it.
                    readonly property var info: root.session ? root.session.groupSenderInfo(memberRow.modelData) : null
                    readonly property bool memberAdmin: root.session && root.session.memberIsAdmin(memberRow.modelData)
                    readonly property bool isContact: root.session && root.session.contactsRevision >= 0
                        && root.session.isContact(memberRow.modelData)
                    Avatar { fingerprint: memberRow.modelData; size: 32 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                text: memberRow.info ? memberRow.info.name : memberRow.modelData
                                color: (memberRow.info && memberRow.info.isContact) ? Theme.text : Theme.green
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            // Highlight an admin member.
                            Label {
                                visible: memberRow.memberAdmin
                                text: "admin"
                                color: Theme.green; font.pixelSize: 10; font.weight: Font.Medium
                            }
                        }
                        Label {
                            visible: memberRow.info && memberRow.info.fpShort.length > 0
                            text: memberRow.info ? "(" + memberRow.info.fpShort + ")" : ""
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                    // Chat (an existing contact: opens the 1:1 and closes this panel)
                    // or Add (a non-contact: a direct, member-only contact request).
                    MenuButton {
                        text: memberRow.isContact ? "Chat" : "Add"
                        onClicked: {
                            if (memberRow.isContact) {
                                // Close this panel BEFORE switching conversation:
                                // openConversation clears the active group's member
                                // list, which destroys this very delegate (and the
                                // button), so a close() issued afterwards is swallowed.
                                root.close()
                                root.session.openConversation(memberRow.modelData)
                            } else {
                                root.session.addContactFromGroup(memberRow.modelData)
                            }
                        }
                    }
                    // Admin-only actions (grant/revoke admin, remove) in an overflow menu.
                    IconButton {
                        visible: root.admin
                        text: "⋮"
                        onClicked: memberMenu.popup()
                        Menu {
                            id: memberMenu
                            MenuItem {
                                text: memberRow.memberAdmin ? "Dismiss as admin" : "Make admin"
                                onTriggered: root.session.setGroupAdmin(memberRow.modelData, !memberRow.memberAdmin)
                            }
                            MenuItem {
                                text: "Remove from group"
                                onTriggered: root.session.removeGroupMember(root.session.activePeer, memberRow.modelData)
                            }
                        }
                    }
                }
            }

            // --- Add members (admin) ---
            Label { visible: root.addMode; text: "Add contacts to the group:"; color: Theme.textDim }
            // Selectable contact rows in the shared terminal style (solid surface,
            // neon outline on hover/selection, avatar + readable name + a check on
            // the right), so the picker matches Settings and New chat - not the stock
            // CheckDelegate (dark-on-dark text, a too-bright full-row highlight that
            // swallowed the checkbox).
            ListView {
                visible: root.addMode
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: root.session ? root.session.contacts : null
                delegate: CheckDelegate {
                    id: addRow
                    width: ListView.view ? ListView.view.width : 0
                    readonly property bool already: root.session
                        && root.session.activeGroupMembers.indexOf(model.fingerprint) >= 0
                    visible: !model.isGroup && !already
                    height: (model.isGroup || already) ? 0 : 52
                    hoverEnabled: true
                    leftPadding: 10
                    rightPadding: 42
                    checked: root.selectedFps.indexOf(model.fingerprint) >= 0
                    onToggled: {
                        var a = root.selectedFps.slice()
                        var i = a.indexOf(model.fingerprint)
                        if (checked && i < 0) a.push(model.fingerprint)
                        else if (!checked && i >= 0) a.splice(i, 1)
                        root.selectedFps = a
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: addRow.down ? Theme.border2 : (addRow.hovered ? Theme.surfaceAlt : Theme.bg)
                        border.width: 1
                        border.color: (addRow.checked || addRow.hovered) ? Theme.green : Theme.border
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                    }
                    indicator: Rectangle {
                        implicitWidth: 20; implicitHeight: 20
                        x: addRow.width - width - 12
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 4
                        color: addRow.checked ? Theme.green : "transparent"
                        border.width: 1
                        border.color: addRow.checked ? Theme.green : Theme.border2
                        Label {
                            anchors.centerIn: parent
                            visible: addRow.checked
                            text: "✓"
                            color: Theme.bg
                            font.pixelSize: 14
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Avatar { fingerprint: model.fingerprint; size: 30 }
                        Label {
                            Layout.fillWidth: true
                            text: root.session ? root.session.peerName(model.fingerprint) : model.fingerprint
                            color: Theme.text
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
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
