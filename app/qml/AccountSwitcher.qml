import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// The unified account list: every profile on this device, with live status.
// Tap a row to make it the active (focused) account; flip its switch to take it
// online (receiving) or offline. Several accounts stay online at once.
Popup {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 80 : 560, 560)
    // Wide enough for the widest count the badge draws.
    readonly property int unreadSlotWidth: 34
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    function statusText(m) {
        if (!m.open) {
            return m.encrypted ? "🔒 Locked" : "Offline"
        }
        if (!m.online) {
            return "Offline"
        }
        return m.connected ? "Online" : "Connecting…"
    }
    function statusColor(m) {
        if (m.open && m.online && m.connected) {
            return Theme.success
        }
        if (m.open && m.online) {
            return Theme.warn   // connecting
        }
        return Theme.textDim   // offline / locked
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label { text: "Profiles"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ListView {
            id: accountList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.accounts
            // Switching and closing live here, not in the delegate's onClicked: a
            // compiled signal handler inside a delegate cannot resolve the enclosing
            // Popup's id, so the delegate calls in through ListView.view instead.
            function activate(accountId) {
                App.switchTo(accountId)
                root.close()
            }
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 68
                onClicked: ListView.view.activate(model.accountId)
                // The active account is marked with a neon outline, not a bright
                // accent fill (the brand's one-accent rule; gray stays the base).
                background: Rectangle {
                    color: "transparent"
                    radius: Theme.radiusSmall
                    border.color: model.active ? Theme.neon : "transparent"
                    border.width: model.active ? 1 : 0
                }
                contentItem: RowLayout {
                    spacing: 12
                    Avatar { fingerprint: model.fingerprint; size: 42 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        // The active account is marked by its name glowing neon
                        // green (plus the row's neon outline), not a separate label.
                        Label {
                            text: model.name
                            color: model.active ? Theme.neon : Theme.text
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.maximumWidth: 200
                        }
                        RowLayout {
                            spacing: 6
                            Rectangle { Layout.alignment: Qt.AlignVCenter; implicitWidth: 7; implicitHeight: 7; radius: 3.5; color: root.statusColor(model) }
                            Label { text: root.statusText(model); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                            // The active connection, as a chip of fixed width: a
                            // positive green "I2P" over an I2P facade, grey "web"
                            // over a clearnet one, and "off" when the profile is
                            // not connected at all. Every row carries one, so the
                            // grid does not drift between connected and idle rows.
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                readonly property bool live: model.open && model.online
                                    && model.connected && model.activeFacade.length > 0
                                radius: Theme.radiusSmall
                                color: live && model.i2pFacade ? Theme.green : "transparent"
                                border.color: live
                                    ? (model.i2pFacade ? Theme.green : Theme.border)
                                    : Theme.border
                                border.width: 1
                                implicitHeight: connLabel.implicitHeight + 4
                                // Sized for the widest label so the row keeps its
                                // shape when the state changes under it.
                                implicitWidth: 44
                                Label {
                                    id: connLabel
                                    anchors.centerIn: parent
                                    text: !parent.live ? "OFF" : (model.i2pFacade ? "I2P" : "web")
                                    color: parent.live && model.i2pFacade ? Theme.text : Theme.textDim
                                    font.pixelSize: Theme.fontSmall - 1
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }
                    // The count sits in a slot of its own, so arriving or
                    // clearing messages never shift the switch beside it.
                    Item {
                        Layout.preferredWidth: root.unreadSlotWidth
                        Layout.fillHeight: true
                        UnreadBadge {
                            anchors.centerIn: parent
                            count: model.unread
                        }
                    }
                    // Per-account online/offline toggle, last in the row.
                    Toggle {
                        Layout.alignment: Qt.AlignVCenter
                        checked: model.online
                        onToggled: App.setOnline(model.accountId, checked)
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        Button {
            Layout.fillWidth: true
            Layout.margins: 12
            // The picker behind this button also opens, removes and imports
            // profiles, so it is not an "add" button.
            text: "Manage"
            hoverEnabled: true
            onClicked: { root.close(); App.requestAddAccount() }
            background: Rectangle { radius: 10; color: parent.down ? Qt.darker(Theme.accent, 1.2) : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                Icon { name: "gear"; color: Theme.accentText; size: 15 }
                Label { text: "Manage"; color: Theme.accentText }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
