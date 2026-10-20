import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null
    signal newChatRequested()
    signal settingsRequested()
    signal appSettingsRequested()
    // Too narrow for two panes and a row of labelled actions: one menu instead.
    readonly property bool narrow: width < 300
    signal accountsRequested()

    function formatTime(ts) {
        if (!ts) return ""
        return new Date(ts).toLocaleTimeString(Qt.locale(), "hh:mm")
    }

    // One-line, length-capped preview so a long or multi-line message never
    // breaks the chat row: newlines/whitespace collapse to single spaces and the
    // text is cut to 30 characters.
    function previewText(s) {
        if (!s) {
            return ""
        }
        const oneLine = App.markupPlain(s).replace(/\s+/g, " ").trim()
        return oneLine.length > 30 ? oneLine.substring(0, 30) + "…" : oneLine
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: own account + actions.
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
                        Icon { name: "chevron"; color: Theme.textDim; size: 14 }
                        // Unread in the accounts that are not on screen: this is
                        // the control that leads to them.
                        UnreadBadge { count: App.unreadElsewhere }
                    }
                    TapHandler { onTapped: root.accountsRequested() }
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    visible: !root.narrow
                    iconName: "gear"
                    onClicked: root.appSettingsRequested()
                }
                IconButton {
                    visible: root.narrow
                    iconName: "burger"
                    onClicked: narrowMenu.open()
                    ContextMenu {
                        id: narrowMenu
                        y: parent.height
                        ContextMenuItem { text: qsTr("New chat"); onTriggered: root.newChatRequested() }
                        ContextMenuItem { text: qsTr("Account"); onTriggered: root.settingsRequested() }
                        ContextMenuItem { text: qsTr("Global settings"); onTriggered: root.appSettingsRequested() }
                    }
                }
            }
        }
        Hairline { }

        // Search the chat list by name (filters the list as you type). Clears when
        // the field is emptied.
        TextField {
            id: chatSearch
            Layout.fillWidth: true
            Layout.margins: 8
            placeholderText: qsTr("Search chats…")
            color: Theme.text
            placeholderTextColor: Theme.textDim
            leftPadding: 10
            rightPadding: clearSearch.visible ? clearSearch.width : padding
            selectByMouse: true
            // A phone keyboard's word in progress is in displayText and not yet in text.
            onDisplayTextChanged: if (root.session) { root.session.setChatFilter(displayText) }
            background: Rectangle {
                radius: 8
                color: Theme.surface
                border.color: chatSearch.activeFocus ? Theme.green : Theme.border
            }
            IconButton {
                id: clearSearch
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height
                width: height
                visible: chatSearch.displayText.length > 0
                iconName: "close"
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Clear")
                onClicked: chatSearch.clear()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.session ? root.session.chatList : null
            topMargin: Theme.rowInset / 2
            bottomMargin: Theme.rowInset / 2
            delegate: ItemDelegate {
                id: chatRow
                width: ListView.view.width
                height: 66
                leftInset: Theme.rowInset
                rightInset: Theme.rowInset
                topInset: Theme.rowInset / 2
                bottomInset: Theme.rowInset / 2
                leftPadding: Theme.rowInset + Theme.spacing
                rightPadding: Theme.rowInset + Theme.spacing
                highlighted: root.session && root.session.activePeer === model.fingerprint
                onClicked: root.session.openConversation(model.fingerprint)
                onPressAndHold: pinMenu.popup()
                // The default highlight paints a solid near-white fill that breaks
                // the dark look; mark the active chat with a white outline instead
                // (the name turns neon below), and keep a subtle hover tint only.
                background: Rectangle {
                    color: chatRow.hovered ? Theme.surfaceAlt : "transparent"
                    radius: Theme.radiusSmall
                    border.width: chatRow.highlighted ? 1 : 0
                    border.color: Theme.accent
                }
                contentItem: RowLayout {
                    spacing: 10
                    // The saved chat is not a correspondent and has no face: it
                    // carries a bookmark, which is what it is for.
                    Rectangle {
                        visible: model.saved
                        implicitWidth: 44
                        implicitHeight: 44
                        radius: width / 2
                        color: Theme.surfaceAlt
                        Icon {
                            anchors.centerIn: parent
                            name: "bookmark"
                            color: Theme.green
                            size: 22
                        }
                    }
                    Avatar {
                        visible: !model.saved
                        fingerprint: model.fingerprint
                        size: 44
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            // Pin marker: a pinned chat stays at the top of the list.
                            Icon {
                                visible: model.pinned
                                name: "pin"
                                color: Theme.textDim
                                size: 12
                                Layout.alignment: Qt.AlignVCenter
                            }
                            Label {
                                Layout.fillWidth: true
                                text: model.name.length > 14 && !model.saved
                                    ? model.name.substring(0, 12) + "…" : model.name
                                color: chatRow.highlighted ? Theme.neon : Theme.text
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Label { text: root.formatTime(model.lastTime); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: root.previewText(model.lastText)
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                maximumLineCount: 1
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
                // Pin/unpin via right-click or long-press (a left tap still opens the
                // chat). Pin state syncs to the account's other devices, and the
                // saved chat is pinned like any other.
                TapHandler {
                    // A touch carries no button, so a right-button handler would take every tap.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.RightButton
                    onTapped: pinMenu.popup()
                }
                ContextMenu {
                    id: pinMenu
                    ContextMenuItem {
                        text: model.pinned ? qsTr("Unpin chat") : qsTr("Pin to top")
                        onTriggered: root.session.pinChat(model.fingerprint, !model.pinned)
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; visible: !root.narrow }
        Rectangle {
            visible: !root.narrow
            Layout.fillWidth: true
            implicitHeight: Theme.barHeight
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4
                BarButton {
                    iconName: "edit"
                    label: qsTr("New chat")
                    onTapped: root.newChatRequested()
                }
                BarButton {
                    iconName: "person"
                    label: qsTr("Account")
                    onTapped: root.settingsRequested()
                }
            }
        }
    }
    // A bar action: the icon says it at a glance, the word says it exactly.
    component BarButton: Item {
        id: barButton
        property string iconName: ""
        property string label: ""
        signal tapped()
        Layout.fillWidth: true
        Layout.fillHeight: true
        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: 8
            color: tap.pressed ? Theme.border2 : (hover.hovered ? Theme.surfaceAlt : "transparent")
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 2
                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: barButton.iconName
                    color: hover.hovered ? Theme.text : Theme.textDim
                    size: 17
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: barButton.label
                    color: hover.hovered ? Theme.text : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }
            HoverHandler { id: hover }
            TapHandler { id: tap; onTapped: barButton.tapped() }
        }
    }
}
