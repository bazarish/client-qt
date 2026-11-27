// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    bottomMargin: typing ? App.keyboardInset : 0
    property var session: null
    readonly property string shareLink: (session && session.activePeer.length > 0
        && session.contactsRevision >= 0) ? session.contactInvite(session.activePeer) : ""
    readonly property bool saved: session !== null && session.isSavedChat(session.activePeer)
    readonly property bool inGroup: session !== null && session.activeIsGroup
    readonly property bool bot: session !== null && session.contactsRevision >= 0
        && session.contactKind(session.activePeer) === "bot"
    readonly property bool blocked: session !== null && session.contactsRevision >= 0
        && session.isBlocked(session.activePeer)
    readonly property bool switchedOn: session !== null && session.online
    signal shareRequested()
    signal membersRequested()

    readonly property var peerDevices: (session && session.contactDevicesRevision >= 0)
        ? session.contactDevices(session.activePeer) : []

    onOpened: {
        statusBlock.expanded = false
        nameField.text = (session ? session.contactName(session.activePeer) : "")
        if (session && !root.bot && !root.inGroup) {
            session.refreshContactDevices(session.activePeer)
        }
    }

    function saveName() {
        if (session) {
            session.renameContact(session.activePeer, nameField.text)
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: root.saved ? qsTr("Saved messages") : qsTr("Contact")
            onCloseClicked: root.close()
        }

        ScrollColumn {
            id: infoColumn
            Layout.fillWidth: true
            Layout.fillHeight: true
            inset: Theme.contentInset
            spacing: 12

            SavedMark {
                visible: root.saved
                Layout.alignment: Qt.AlignHCenter
                size: 88
            }
            Label {
                visible: root.saved
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: qsTr("What you keep here reaches your other devices")
            }
            Label {
                visible: root.saved
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: qsTr("A device that joins later starts empty: to have something on a new device, "
                    + "send or forward it again")
            }
            MenuButton {
                visible: root.saved
                Layout.fillWidth: true
                Layout.topMargin: 4
                iconName: "trash"
                text: qsTr("Clear only here")
                onClicked: {
                    clearSavedDialog.everywhere = false
                    clearSavedDialog.open()
                }
            }
            MenuButton {
                visible: root.saved
                Layout.fillWidth: true
                iconName: "trash"
                text: qsTr("Clear on all devices")
                danger: true
                onClicked: {
                    clearSavedDialog.everywhere = true
                    clearSavedDialog.open()
                }
            }

            Avatar {
                visible: !root.saved
                Layout.alignment: Qt.AlignHCenter
                fingerprint: root.session ? root.session.activePeer : ""
                size: 88
                enlargeable: true
            }
            Label {
                visible: !root.saved
                Layout.alignment: Qt.AlignHCenter
                text: (root.session && root.session.contactsRevision >= 0)
                    ? root.session.peerName(root.session.activePeer) : ""
                color: Theme.text
                font.weight: Font.Medium
                font.pixelSize: Theme.fontTitle
            }
            ColumnLayout {
                id: statusBlock
                readonly property string status: root.session && root.session.contactsRevision >= 0
                    ? root.session.contactStatus(root.session.activePeer) : ""
                property bool expanded: false
                visible: !root.saved && status.length > 0
                Layout.fillWidth: true
                spacing: 2
                Label {
                    id: statusText
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: App.linksHtml(statusBlock.status)
                    textFormat: Text.StyledText
                    linkColor: Theme.accent
                    color: Theme.textDim
                    wrapMode: Text.Wrap
                    maximumLineCount: statusBlock.expanded ? undefined : 1
                    elide: Text.ElideRight
                    onLinkActivated: function(link) {
                        if (App.isAppLink(link)) {
                            App.openLink(link)
                        } else {
                            statusLinkWarning.ask(link)
                        }
                    }
                    HoverHandler {
                        cursorShape: statusText.hoveredLink.length > 0
                            ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }
                Label {
                    visible: statusText.truncated || statusBlock.expanded
                    Layout.alignment: Qt.AlignHCenter
                    text: statusBlock.expanded ? qsTr("Show less") : qsTr("Show more")
                    color: Theme.green
                    font.pixelSize: Theme.fontSmall
                    TapHandler { onTapped: statusBlock.expanded = !statusBlock.expanded }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
            Label {
                visible: !root.saved
                text: qsTr("Display name:")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            RowLayout {
                visible: !root.saved
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: root.session
                        ? root.session.shortFingerprint(root.session.activePeer) : qsTr("name")
                    color: Theme.text
                    placeholderTextColor: Theme.textDim
                    onAccepted: root.saveName()
                }
                MenuButton { iconName: "check"; text: qsTr("Save"); onClicked: root.saveName() }
            }
            RowLayout {
                visible: !root.saved
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: fingerprintLine.copied ? qsTr("Copied to clipboard") : qsTr("Identity fingerprint")
                    color: fingerprintLine.copied ? Theme.green : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Fingerprint {
                    id: fingerprintLine
                    property bool copied: false
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    value: root.session ? root.session.activePeer : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    Timer {
                        id: fingerprintCopied
                        interval: 1500
                        onTriggered: fingerprintLine.copied = false
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (!root.session) {
                                return
                            }
                            App.copyText(root.session.activePeer)
                            fingerprintLine.copied = true
                            fingerprintCopied.restart()
                        }
                    }
                }
            }

            GridLayout {
                id: contactSwitches
                visible: !root.saved
                Layout.fillWidth: true
                Layout.topMargin: 2
                columnSpacing: 12
                rowSpacing: 8
                readonly property int sideBySideWidth: 360
                columns: infoColumn.columnWidth < sideBySideWidth ? 2 : 4
                Label {
                    text: qsTr("Notifications")
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: contactSwitches.columns === 2
                }
                Toggle {
                    checked: root.session !== null && root.session.contactsRevision >= 0
                        && root.session.contactNotifications(root.session.activePeer)
                    onToggled: root.session.setContactNotifications(root.session.activePeer, checked)
                }
                Label {
                    visible: !root.inGroup
                    text: qsTr("Allow calls")
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    horizontalAlignment: contactSwitches.columns === 4 ? Text.AlignRight : Text.AlignLeft
                }
                Toggle {
                    visible: !root.inGroup
                    checked: root.session !== null && root.session.contactsRevision >= 0
                        && root.session.contactCalls(root.session.activePeer)
                    onToggled: root.session.setContactCalls(root.session.activePeer, checked)
                }
            }

            RowLayout {
                visible: !root.inGroup
                Layout.fillWidth: true
                spacing: 12
                Label {
                    text: qsTr("Auto-delete")
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Dropdown {
                    id: timerBox
                    model: root.session
                        ? root.session.chatTimerChoices().map(seconds => ({
                            seconds: seconds,
                            label: Tr.language.length >= 0 ? root.session.chatTimerLabel(seconds) : ""
                        }))
                        : []
                    textRole: "label"
                    valueRole: "seconds"
                    Layout.preferredWidth: implicitWidth
                    function follow() {
                        currentIndex = root.session ? indexOfValue(root.session.activeChatTimer) : -1
                    }
                    onModelChanged: follow()
                    Component.onCompleted: follow()
                    onActivated: root.session.setChatTimer(currentValue)
                    Connections {
                        target: root.session
                        function onActiveChatTimerChanged() { timerBox.follow() }
                    }
                }
            }

            Rectangle {
                visible: !root.saved
                Layout.fillWidth: true
                height: 1
                color: Theme.border
                Layout.topMargin: 4
            }


            MenuButton {
                visible: !root.saved && root.shareLink.length > 0
                Layout.fillWidth: true
                iconName: "link"
                text: qsTr("Share contact")
                onClicked: { root.close(); root.shareRequested() }
            }

            MenuButton {
                visible: !root.saved && root.inGroup && root.session !== null
                    && root.session.activeMemberListHeld
                Layout.fillWidth: true
                iconName: "people"
                text: qsTr("Member list")
                onClicked: { root.close(); root.membersRequested() }
            }

            Label {
                visible: !root.saved && root.blocked
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.warn
                font.pixelSize: Theme.fontSmall
                text: qsTr("Blocked: messages, calls and contact requests from this contact are dropped, and its pass is revoked. An outgoing message lifts the block.")
            }

            Rectangle { visible: !root.saved; Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 4 }

            ColumnLayout {
                visible: !root.saved
                Layout.fillWidth: true
                spacing: 8
                MenuButton {
                    Layout.fillWidth: true
                    iconName: "trash"
                    text: qsTr("Clear chat")
                    onClicked: clearChoiceDialog.open()
                }
                ButtonRow {
                    Layout.fillWidth: true
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: root.blocked ? "check" : "block"
                        text: root.blocked ? qsTr("Unblock") : qsTr("Block")
                        danger: !root.blocked
                        enabled: root.switchedOn
                        onClicked: {
                            if (root.blocked) {
                                root.session.setBlocked(root.session.activePeer, false)
                            } else {
                                blockDialog.open()
                            }
                        }
                    }
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: "trash"
                        text: qsTr("Delete contact")
                        danger: true
                        enabled: root.switchedOn
                        onClicked: deleteContactDialog.open()
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: 2
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                readonly property bool mayWrite: {
                    // Read so the binding re-evaluates on contact changes: the call alone is not tracked.
                    const revision = root.session ? root.session.contactsRevision : 0
                    return root.session ? root.session.canWriteTo(root.session.activePeer) : false
                }
                visible: !root.saved && root.session && root.session.activePeer.length > 0 && !mayWrite
                text: qsTr("This contact has not allowed you to write")
                wrapMode: Text.Wrap
            }


            ColumnLayout {
                visible: !root.saved && !root.inGroup && !root.bot && root.peerDevices.length > 0
                Layout.fillWidth: true
                spacing: 6
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 4 }
                Label {
                    text: qsTr("Contact's devices")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Repeater {
                    model: root.peerDevices
                    delegate: Fingerprint {
                        required property var modelData
                        value: modelData.deviceId
                        whole: true
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }

    Dialog {
        id: clearSavedDialog
        property bool everywhere: true
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Clear saved messages")
            showClose: false
        }
        footer: DialogButtons {
            acceptText: clearSavedDialog.everywhere ? qsTr("Clear everywhere") : qsTr("Clear here")
            rejectText: qsTr("Cancel")
            danger: true
            onAccepted: clearSavedDialog.accept()
            onRejected: clearSavedDialog.reject()
        }
        onAccepted: {
            if (root.session) {
                if (clearSavedDialog.everywhere) {
                    root.session.clearSavedEverywhere()
                } else {
                    root.session.clearSavedHere()
                }
            }
            root.close()
        }
        contentItem: Label {
            padding: 14
            wrapMode: Text.Wrap
            color: Theme.text
            text: clearSavedDialog.everywhere
                ? qsTr("Everything kept here goes, on this device and on every other device of this account")
                : qsTr("Everything kept here goes from this device only")
        }
    }

    Dialog {
        id: blockDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Block this contact?")
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Block")
            rejectText: qsTr("Cancel")
            danger: true
            onAccepted: blockDialog.accept()
            onRejected: blockDialog.reject()
        }
        onAccepted: {
            if (root.session) { root.session.setBlocked(root.session.activePeer, true) }
        }
        contentItem: Label {
            padding: 14
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("Messages, calls and contact requests from this contact stop arriving, and the pass it held is revoked at your server. The conversation stays. An outgoing message lifts the block.")
        }
    }

    Dialog {
        id: clearChoiceDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Clear chat")
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Cancel")
            showReject: false
            onAccepted: clearChoiceDialog.close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: qsTr("Remove every message in this chat. The chat itself stays.")
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "person"
                text: qsTr("Clear only for me")
                onClicked: {
                    if (root.session) { root.session.clearChat(false) }
                    clearChoiceDialog.close()
                    root.close()
                }
            }
            MenuButton {
                visible: !root.inGroup
                Layout.fillWidth: true
                iconName: "people"
                text: qsTr("Clear for everyone")
                danger: true
                onClicked: {
                    if (root.session) { root.session.clearChat(true) }
                    clearChoiceDialog.close()
                    root.close()
                }
            }
            Label {
                visible: !root.inGroup
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: qsTr("“For everyone” asks the other side’s app to delete its copy too")
            }
        }
    }

    Dialog {
        id: deleteContactDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        background: DialogFrame { destructive: true }
        header: WindowHeader {
            title: qsTr("Delete contact")
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Delete")
            danger: true
            onAccepted: deleteContactDialog.accept()
            onRejected: deleteContactDialog.reject()
        }
        onAccepted: {
            if (root.session) { root.session.deleteContact() }
            root.close()
        }
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("The contact and the conversation are removed on all devices of this account, and its pass is revoked. The contact can send a new contact request. This cannot be undone.")
        }
    }

    LinkWarningDialog { id: statusLinkWarning }
}
