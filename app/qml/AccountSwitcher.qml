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
    width: 420
    height: Math.min(parent ? parent.height - 80 : 560, 560)
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
            Label { text: "Accounts"; color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.accounts
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 68
                highlighted: model.active
                onClicked: { App.switchTo(model.accountId); root.close() }
                contentItem: RowLayout {
                    spacing: 12
                    Avatar { fingerprint: model.fingerprint; size: 42 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            Label { text: model.name; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight; Layout.maximumWidth: 200 }
                            Label {
                                visible: model.active
                                text: "Active"
                                color: Theme.accent
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.Medium
                            }
                        }
                        RowLayout {
                            spacing: 6
                            Rectangle { Layout.alignment: Qt.AlignVCenter; implicitWidth: 7; implicitHeight: 7; radius: 3.5; color: root.statusColor(model) }
                            Label { text: root.statusText(model); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                            // The active connection: a positive green "I2P" pill when the
                            // account is connected over an I2P facade, a grey "web" chip for
                            // a clearnet facade. Only shown while actually connected.
                            Rectangle {
                                visible: model.open && model.online && model.connected && model.activeFacade.length > 0
                                Layout.alignment: Qt.AlignVCenter
                                radius: Theme.radiusSmall
                                color: model.i2pFacade ? Theme.neon : "transparent"
                                border.color: model.i2pFacade ? Theme.neon : Theme.border
                                border.width: 1
                                implicitHeight: connLabel.implicitHeight + 4
                                implicitWidth: connLabel.implicitWidth + 12
                                Label {
                                    id: connLabel
                                    anchors.centerIn: parent
                                    text: model.i2pFacade ? "I2P" : "web"
                                    color: model.i2pFacade ? Theme.text : Theme.textDim
                                    font.pixelSize: Theme.fontSmall - 1
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }
                    Rectangle {
                        visible: model.unread > 0
                        radius: height / 2
                        color: Theme.accent
                        implicitHeight: 20
                        implicitWidth: Math.max(20, ub.implicitWidth + 10)
                        Label { id: ub; anchors.centerIn: parent; text: model.unread; color: Theme.accentText; font.pixelSize: 11 }
                    }
                    // Per-account online/offline toggle.
                    Switch {
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
            text: "➕  Add account"
            onClicked: { root.close(); App.requestAddAccount() }
            background: Rectangle { radius: 10; color: parent.down ? Qt.darker(Theme.accent, 1.2) : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
