import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null
    signal newChatRequested()
    signal settingsRequested()
    signal accountsRequested()

    function formatTime(ts) {
        if (!ts) return ""
        return new Date(ts * 1000).toLocaleTimeString(Qt.locale(), "hh:mm")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: own profile + actions.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 60
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 6
                spacing: 10
                // Tap the avatar or name to open the account switcher.
                Avatar {
                    fingerprint: root.session ? root.session.fingerprint : ""
                    size: 36
                    TapHandler { onTapped: root.accountsRequested() }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    RowLayout {
                        spacing: 4
                        Label {
                            text: root.session ? root.session.displayName : ""
                            color: Theme.text
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.maximumWidth: 180
                        }
                        Label { text: "⌄"; color: Theme.textDim }
                    }
                    TapHandler { onTapped: root.accountsRequested() }
                }
                IconButton { text: "✎"; onClicked: root.newChatRequested() }
                IconButton { text: "⚙"; onClicked: root.settingsRequested() }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.session ? root.session.contacts : null
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 66
                highlighted: root.session && root.session.activePeer === model.fingerprint
                onClicked: root.session.openConversation(model.fingerprint)
                contentItem: RowLayout {
                    spacing: 10
                    Avatar { fingerprint: model.fingerprint; size: 44 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: (model.isGroup ? "👥 " : "")
                                    + (model.name.length > 14 ? model.name.substring(0, 12) + "…" : model.name)
                                color: Theme.text
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Label { text: root.formatTime(model.lastTime); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: model.lastText
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                visible: model.unread > 0
                                radius: height / 2
                                color: Theme.accent
                                implicitHeight: 18
                                implicitWidth: Math.max(18, badge.implicitWidth + 10)
                                Label { id: badge; anchors.centerIn: parent; text: model.unread; color: Theme.accentText; font.pixelSize: 11 }
                            }
                        }
                    }
                }
            }
        }

        // Connection status plate: makes a missing or pending server connection
        // obvious at the bottom-left without opening any menu. "Offline" when the
        // account is not syncing; "Connecting…" while online but not yet reaching
        // the server. Tapping it opens Settings, where the connection is managed.
        Rectangle {
            id: connPlate
            Layout.fillWidth: true
            property bool isOffline: root.session && !root.session.online
            property bool isConnecting: root.session && root.session.online && !root.session.reachable
            visible: connPlate.isOffline || connPlate.isConnecting
            implicitHeight: plateRow.implicitHeight + 16
            color: Theme.surface

            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                id: plateRow
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 8; implicitHeight: 8; radius: 4
                    color: connPlate.isOffline ? Theme.textDim : Theme.warn
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: connPlate.isOffline ? "Offline" : "Connecting…"
                        color: connPlate.isOffline ? Theme.textDim : Theme.warn
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                    }
                    Label {
                        text: connPlate.isOffline ? "This account is not syncing"
                                                  : "No server connection yet"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                Label {
                    text: "⚙"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontBody
                }
            }
            TapHandler { onTapped: root.settingsRequested() }
        }
    }
}
