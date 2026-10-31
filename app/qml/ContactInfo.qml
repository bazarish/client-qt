// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    bottomMargin: activeFocus ? App.keyboardInset : 0
    property var session: null
    readonly property string shareLink: (session && session.activePeer.length > 0
        && session.contactsRevision >= 0) ? session.contactInvite(session.activePeer) : ""
    readonly property bool saved: session !== null && session.isSavedChat(session.activePeer)
    readonly property bool inGroup: session !== null && session.activeIsGroup
    readonly property bool blocked: session !== null && session.contactsRevision >= 0
        && session.isBlocked(session.activePeer)
    signal shareRequested()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    height: Math.min(parent ? parent.height - 40 : 620, body.implicitHeight + 36)
    padding: 18

    background: DialogFrame { }

    onOpened: nameField.text = (session ? session.contactName(session.activePeer) : "")

    function saveName() {
        if (session) {
            session.renameContact(session.activePeer, nameField.text)
        }
    }

    contentItem: ScrollView {
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
        id: body
        width: root.width - 36
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.saved ? qsTr("Saved messages") : qsTr("Contact")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

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
            text: qsTr("What you keep here reaches your other devices.")
        }
        Label {
            visible: root.saved
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            text: qsTr("A device that joins later starts empty: to have something on a new device, "
                + "send or forward it again.")
        }
        MenuButton {
            visible: root.saved
            Layout.fillWidth: true
            Layout.topMargin: 4
            iconName: "trash"
            text: qsTr("Clear on all devices")
            danger: true
            onClicked: clearSavedDialog.open()
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
                background: Rectangle { radius: 8; color: Theme.surface; border.color: nameField.activeFocus ? Theme.accent : Theme.border }
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
            Label {
                id: fingerprintLine
                property bool copied: false
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: root.session ? root.session.activePeer : ""
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideMiddle
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
            columns: width < sideBySideWidth ? 2 : 4
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

        Label {
            visible: !root.saved && root.blocked
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.warn
            font.pixelSize: Theme.fontSmall
            text: qsTr("Blocked: messages and calls from this contact are dropped, and its pass is revoked. An outgoing message lifts the block.")
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
            visible: !root.saved && root.session && root.session.activePeer.length > 0
            text: {
                // Read so the binding re-evaluates on contact changes: the call alone is not tracked.
                const revision = root.session ? root.session.contactsRevision : 0
                const may = root.session ? root.session.canWriteTo(root.session.activePeer) : false
                return may ? qsTr("You can write to this contact")
                           : qsTr("This contact has not allowed you to write")
            }
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: clearSavedDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Clear saved messages")
            color: Theme.green
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: qsTr("Clear everywhere")
            rejectText: qsTr("Cancel")
            danger: true
            onAccepted: clearSavedDialog.accept()
            onRejected: clearSavedDialog.reject()
        }
        onAccepted: {
            if (root.session) { root.session.clearSavedEverywhere() }
            root.close()
        }
        contentItem: Label {
            padding: 14
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("Everything kept here goes, on this device and on every other device of this account.")
        }
    }

    Dialog {
        id: blockDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(380, parent ? parent.width - 24 : 380)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Block this contact?")
            color: Theme.green
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
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
            text: qsTr("Messages and calls from this contact stop arriving, and the pass it held is revoked at your server. The conversation stays. An outgoing message lifts the block.")
        }
    }

    Dialog {
        id: clearChoiceDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Clear chat")
            color: Theme.green
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
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
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: qsTr("“For everyone” asks the other side’s app to delete its copy too.")
            }
        }
    }

    Dialog {
        id: deleteContactDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: DialogFrame { destructive: true }
        header: Label {
            text: qsTr("Delete contact")
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
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
            text: qsTr("Removes this contact and your whole history with them from this device. This cannot be undone.")
        }
        }
    }
}
