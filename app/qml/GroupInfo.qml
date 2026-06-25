import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Group panel: member list, admin add/remove, and leave. Opened from the
// conversation header's i for a group.
Popup {
    id: root
    property var session: null
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(parent ? parent.height - 40 : 600, 620)
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    property bool addMode: false
    property var selectedFps: []
    onOpened: { addMode = false; selectedFps = [] }

    readonly property bool admin: root.session ? root.session.activeGroupAdmin : false

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
                    width: ListView.view.width
                    height: 48
                    spacing: 10
                    Avatar { fingerprint: modelData; size: 32 }
                    Label {
                        Layout.fillWidth: true
                        text: root.session ? root.session.shortFingerprint(modelData) : modelData
                        color: Theme.text; elide: Text.ElideRight
                    }
                    MenuButton {
                        visible: root.admin
                        text: "Remove"
                        danger: true
                        onClicked: root.session.removeGroupMember(root.session.activePeer, modelData)
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
                    text: root.session ? root.session.shortFingerprint(model.fingerprint) : model.fingerprint
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
