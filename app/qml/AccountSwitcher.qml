// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    Connections {
        target: App
        function onAccountOpened() { root.close() }
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 80 : 560, 560)
    readonly property int unreadSlotWidth: 34
    padding: 0
    background: DialogFrame { }

    function statusText(m) {
        if (!m.open) {
            return m.encrypted ? qsTr("🔒 Locked") : qsTr("Disconnected")
        }
        if (!m.online) {
            return qsTr("Disconnected")
        }
        return m.connected ? qsTr("Connected") : qsTr("Connecting…")
    }
    function statusColor(m) {
        if (m.open && m.online && m.connected) {
            return Theme.success
        }
        if (m.open && m.online) {
            return Theme.warn
        }
        return Theme.textDim
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.accounts
            // A delegate's compiled handler cannot resolve the Popup's id, so it calls in through ListView.view.
            function activate(accountId, locked) {
                App.switchTo(accountId)
                if (!locked) {
                    root.close()
                }
            }
            topMargin: Theme.rowInset / 2
            bottomMargin: Theme.rowInset / 2
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 68
                leftInset: Theme.rowInset
                rightInset: Theme.rowInset
                topInset: Theme.rowInset / 2
                bottomInset: Theme.rowInset / 2
                leftPadding: Theme.rowInset + Theme.spacing
                rightPadding: Theme.rowInset + Theme.spacing
                onClicked: ListView.view.activate(model.accountId, model.encrypted && !model.open)
                background: Rectangle {
                    color: "transparent"
                    radius: Theme.radiusSmall
                    border.color: model.active ? Theme.neon : "transparent"
                    border.width: model.active ? 1 : 0
                }
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
                            // Toggling breaks the binding: rebound so a refused unlock does not leave the switch on.
                            checked = Qt.binding(function() { return model.online })
                        }
                    }
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
            focusPolicy: Theme.pressFocus
            Layout.fillWidth: true
            Layout.margins: 12
            text: qsTr("Manage")
            hoverEnabled: true
            onClicked: { root.close(); App.requestAddAccount() }
            background: Rectangle { radius: 10; color: parent.down ? Theme.accentDown : (parent.hovered ? Theme.accentHover : Theme.accent) }
            contentItem: IconLabel { name: "gear"; color: Theme.accentText }
        }
    }
}
