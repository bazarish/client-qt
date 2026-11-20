// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    readonly property int kWindowWidth: 600

    readonly property var session: App.session
    readonly property var info: root.session ? root.session.deviceStorage : ({})
    readonly property bool busy: root.info.busy === true
    readonly property var chats: root.info.chats !== undefined ? root.info.chats : []

    preferredWidth: root.kWindowWidth

    onOpened: if (root.session) { root.session.measureDeviceStorage() }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Storage")
            showBack: root.returnTo !== null
            onBackClicked: root.goBack()
            onCloseClicked: root.close()
        }
        Label {
            visible: root.session !== null
            text: root.session ? root.session.displayName : ""
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: Theme.contentInset
            Layout.rightMargin: Theme.contentInset
            Layout.topMargin: Theme.contentInset
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: root.width
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 4
                    Label {
                        text: qsTr("Database file: %1").arg(App.humanBytes(root.info.fileBytes))
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: root.info.freeBytes > 0
                        text: qsTr("About %1 of it is free space compacting returns to the disk")
                            .arg(App.humanBytes(root.info.freeBytes))
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            iconName: "disk"
                            text: qsTr("Compact the database")
                            enabled: !root.busy && root.session !== null
                            onClicked: root.session.compactDatabase()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            iconName: "trash"
                            text: qsTr("Trim every chat")
                            danger: true
                            enabled: !root.busy && root.chats.length > 0
                            onClicked: {
                                trimDialog.peer = ""
                                trimDialog.who = "every chat"
                                trimDialog.open()
                            }
                        }
                    }
                }
                Hairline { }

                Label {
                    visible: root.chats.length === 0
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    text: root.session ? qsTr("Nothing stored yet") : qsTr("No account is open")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: root.chats
                    delegate: RowLayout {
                        id: chatRow
                        required property var modelData
                        readonly property bool saved: root.session !== null
                            && root.session.isSavedChat(chatRow.modelData.peer)
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.contentInset
                        Layout.rightMargin: Theme.contentInset
                        spacing: 10
                        SavedMark {
                            visible: chatRow.saved
                            size: 34
                            iconSize: 18
                        }
                        Avatar { visible: !chatRow.saved; fingerprint: chatRow.modelData.peer; size: 34 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: chatRow.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: "%1 (%2)"
                                    .arg(chatRow.modelData.messages)
                                    .arg(App.humanBytes(chatRow.modelData.bytes))
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: chatRow.modelData.mediaCount > 0
                                text: qsTr("%1 pictures and voice notes are part of that")
                                    .arg(chatRow.modelData.mediaCount)
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                        MenuButton {
                            iconName: "trash"
                            text: qsTr("Trim")
                            enabled: !root.busy
                            onClicked: {
                                trimDialog.peer = chatRow.modelData.peer
                                trimDialog.who = chatRow.modelData.name
                                trimDialog.open()
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    Layout.topMargin: 4
                    text: qsTr("Trimming happens only on this device")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Dialog {
        id: trimDialog
        property string peer: ""
        property string who: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Trim %1").arg(trimDialog.who)
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Cancel")
            showReject: false
            onAccepted: trimDialog.close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: qsTr("Keeps the newest messages and removes the rest with their pictures and voice notes. Only on this device, and it cannot be undone.")
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "trash"
                text: qsTr("Keep the last %1").arg(root.session ? root.session.keepRecentMessages : 0)
                danger: true
                onClicked: trimDialog.run(root.session ? root.session.keepRecentMessages : 0)
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "trash"
                text: qsTr("Keep the last %1").arg(root.session ? root.session.keepManyMessages : 0)
                danger: true
                onClicked: trimDialog.run(root.session ? root.session.keepManyMessages : 0)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: qsTr("The app may not respond for a few seconds during the operation")
            }
        }

        function run(keep) {
            if (!root.session) {
                return
            }
            if (trimDialog.peer.length > 0) {
                root.session.trimChat(trimDialog.peer, keep)
            } else {
                root.session.trimEveryChat(keep)
            }
            trimDialog.close()
        }
    }

    Popup {
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        visible: root.busy
        closePolicy: Popup.NoAutoClose
        padding: 20
        background: DialogFrame { }
        contentItem: ColumnLayout {
            spacing: 6
            Label {
                text: (root.info.busyWhat !== undefined ? root.info.busyWhat : qsTr("Working")) + "\u2026"
                color: Theme.text
            }
            Label {
                text: qsTr("The window does not answer while this runs")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
}
