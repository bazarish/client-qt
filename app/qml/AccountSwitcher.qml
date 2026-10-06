import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// The unified account list: every account on this device, with live status.
// Tap a row to make it the active (focused) account; flip its switch to take it
// online (receiving) or offline. Several accounts stay online at once.
Popup {
    id: root
    // The account opened (a passphrase was right, or it needed none): this list
    // has done its job.
    Connections {
        target: App
        function onAccountOpened() { root.close() }
    }

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 80 : 560, 560)
    // Wide enough for the widest count the badge draws.
    readonly property int unreadSlotWidth: 34
    padding: 0
    background: DialogFrame { }

    function statusText(m) {
        if (!m.open) {
            return m.encrypted ? qsTr("🔒 Locked") : qsTr("Offline")
        }
        if (!m.online) {
            return qsTr("Offline")
        }
        return m.connected ? qsTr("Online") : qsTr("Connecting…")
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
            Label { text: qsTr("Accounts"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        ListView {
            id: accountList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.accounts
            // Switching and closing live here, not in the delegate's onClicked: a
            // compiled signal handler inside a delegate cannot resolve the enclosing
            // Popup's id, so the delegate calls in through ListView.view instead.
            function activate(accountId, locked) {
                App.switchTo(accountId)
                // A locked account is not switched to yet - a prompt has opened
                // over this list. Closing it here would take the list away too,
                // and a wrong passphrase would leave the user looking at whatever
                // account happened to be open.
                if (!locked) {
                    root.close()
                }
            }
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 68
                onClicked: ListView.view.activate(model.accountId, model.encrypted && !model.open)
                // The active account is marked with a neon outline, not a bright
                // accent fill (the brand's one-accent rule; gray stays the base).
                background: Rectangle {
                    color: "transparent"
                    radius: Theme.radiusSmall
                    border.color: model.active ? Theme.neon : "transparent"
                    border.width: model.active ? 1 : 0
                }
                // Anchored, not laid out: the avatar sits at the left edge, the
                // name a fixed step from it, the switch at the right edge. Nothing
                // in between - a longer name, a wider status, a badge that comes
                // and goes - can move any of them.
                contentItem: Item {
                    Avatar {
                        id: rowAvatar
                        fingerprint: model.fingerprint
                        size: 42
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Toggle {
                        id: rowToggle
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: model.online
                        onToggled: {
                            App.setOnline(model.accountId, checked)
                            // Toggling breaks the binding, and then a switch that
                            // asked for a passphrase and did not get one would
                            // stay on while the account stayed off. Bind it again
                            // so what is on disk is what is shown.
                            checked = Qt.binding(function() { return model.online })
                        }
                    }
                    // The count sits in a slot of its own, so arriving or clearing
                    // messages never shift the switch beside it.
                    Item {
                        id: rowUnread
                        width: root.unreadSlotWidth
                        height: parent.height
                        anchors.right: rowToggle.left
                        anchors.rightMargin: 8
                        UnreadBadge {
                            anchors.centerIn: parent
                            count: model.unread
                        }
                    }
                    Column {
                        anchors.left: rowAvatar.right
                        anchors.leftMargin: 12
                        anchors.right: rowUnread.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        clip: true
                        // The active account is marked by its name glowing neon
                        // green (plus the row's neon outline), not a separate label.
                        Label {
                            width: parent.width
                            text: model.name
                            color: model.active ? Theme.neon : Theme.text
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Row {
                            spacing: 6
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 7
                                height: 7
                                radius: 3.5
                                color: root.statusColor(model)
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.statusText(model)
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                    }
                }
            }
        }
        Hairline { }

        Button {
            Layout.fillWidth: true
            Layout.margins: 12
            // The picker behind this button also opens, removes and imports
            // accounts, so it is not an "add" button.
            text: qsTr("Manage")
            hoverEnabled: true
            onClicked: { root.close(); App.requestAddAccount() }
            background: Rectangle { radius: 10; color: parent.down ? Qt.darker(Theme.accent, 1.2) : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: IconLabel { name: "gear"; color: Theme.accentText }
        }
    }
}
