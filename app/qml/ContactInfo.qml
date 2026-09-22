import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    readonly property string shareLink: (session && session.activePeer.length > 0)
        ? session.contactInvite(session.activePeer) : ""
    // Whether the contact said no, rather than simply not having told us yet.
    // The saved chat is not a contact: this panel says what it is and offers the
    // one thing that can be done to it.
    readonly property bool saved: session !== null && session.isSavedChat(session.activePeer)
    // Re-read whenever anything about the contacts changes, so a switch flipped on
    // another device shows here too.
    readonly property bool blocked: session !== null && session.contactsRevision >= 0
        && session.isBlocked(session.activePeer)
    readonly property bool sharingRefused: session && session.activePeer.length > 0
        && session.contactSharingRefused(session.activePeer)
    signal shareRequested()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 24 : 420)
    // As tall as it needs, capped by the screen, and scrolling inside that cap -
    // it used to size past the bottom of a short screen with no way to reach the
    // rest.
    height: Math.min(parent ? parent.height - 40 : 620, body.implicitHeight + 36)
    padding: 18

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // Prefill the rename field with the current local name each time it opens.
    onOpened: nameField.text = (session ? session.contactName(session.activePeer) : "")

    function saveName() {
        if (session) {
            session.renameContact(session.activePeer, nameField.text)
        }
    }

    contentItem: ScrollView {
        contentWidth: availableWidth
        ColumnLayout {
        id: body
        width: root.width - 36
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.saved ? "Saved messages" : "Contact"
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

        // --- The saved chat ---
        Rectangle {
            visible: root.saved
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 88
            implicitHeight: 88
            radius: width / 2
            color: Theme.surfaceAlt
            Icon { anchors.centerIn: parent; name: "bookmark"; color: Theme.green; size: 44 }
        }
        Label {
            visible: root.saved
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.text
            text: "What you keep here stays on this account and reaches your other devices. "
                + "It is not sent to anybody."
        }
        Label {
            visible: root.saved
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            text: "A device that joins later starts empty: to have something on a new device, "
                + "send or forward it again."
        }
        MenuButton {
            visible: root.saved
            Layout.fillWidth: true
            Layout.topMargin: 4
            iconName: "trash"
            text: "Clear on all devices"
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
        // The contact's display name (the local label, or a short fingerprint).
        Label {
            visible: !root.saved
            Layout.alignment: Qt.AlignHCenter
            // Re-read on every contact change: a rename here is stored through the
            // worker, and the name that comes back may not be the one typed.
            text: (root.session && root.session.contactsRevision >= 0)
                ? root.session.peerName(root.session.activePeer) : ""
            color: Theme.text
            font.weight: Font.Medium
            font.pixelSize: Theme.fontTitle
        }
        // Rename: a purely local label, mirrored only to your own other devices -
        // the contact is never told the name you keep them under.
        Label {
            visible: !root.saved
            text: "Display name (local only):"
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
                placeholderText: root.session ? root.session.shortFingerprint(root.session.activePeer) : "name"
                color: Theme.text
                placeholderTextColor: Theme.textDim
                onAccepted: root.saveName()
                background: Rectangle { radius: 8; color: Theme.surface; border.color: nameField.activeFocus ? Theme.accent : Theme.border }
            }
            MenuButton { iconName: "check"; text: "Save"; onClicked: root.saveName() }
        }
        // The identity itself, on one line: the middle gives way when it does not
        // fit, and a tap anywhere on it copies the whole thing.
        RowLayout {
            visible: !root.saved
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: fingerprintLine.copied ? "Copied to clipboard" : "Identity fingerprint"
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
                        root.session.copyText(root.session.activePeer)
                        fingerprintLine.copied = true
                        fingerprintCopied.restart()
                    }
                }
            }
        }

        // What this contact may do here. The global settings still apply on top:
        // these only take something away, never add it back.
        RowLayout {
            visible: !root.saved
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 12
            Label {
                text: "Notifications"
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
            Toggle {
                checked: root.session !== null && root.session.contactsRevision >= 0
                    && root.session.contactNotifications(root.session.activePeer)
                onToggled: root.session.setContactNotifications(root.session.activePeer, checked)
            }
            Item { Layout.fillWidth: true }
            Label {
                text: "Allow calls"
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
            Toggle {
                checked: root.session !== null && root.session.contactsRevision >= 0
                    && root.session.contactCalls(root.session.activePeer)
                onToggled: root.session.setContactCalls(root.session.activePeer, checked)
            }
        }

        Rectangle {
            // The line divides a contact's identity from what can be done about
            // it. The saved chat has neither, so it has nothing to divide.
            visible: !root.saved
            Layout.fillWidth: true
            height: 1
            color: Theme.border
            Layout.topMargin: 4
        }


        // The label stays short in every state - a button is not the place for a
        // sentence; the reason is written under it.
        MenuButton {
            visible: !root.saved
            Layout.fillWidth: true
            enabled: root.shareLink.length > 0
            iconName: "link"
            text: root.shareLink.length > 0
                ? "Share contact"
                : (root.sharingRefused ? "Sharing is off" : "Not shareable yet")
            onClicked: { root.close(); root.shareRequested() }
        }
        // Said under the button, not on it: the reason is a sentence, and a
        // button wearing one cuts it off in the middle.
        Label {
            visible: !root.saved && root.shareLink.length === 0
            text: root.sharingRefused
                ? "This contact has turned off being passed on."
                : "Their descriptor arrives with their next message."
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: !root.saved && root.blocked
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.warn
            font.pixelSize: Theme.fontSmall
            text: "Blocked: their messages and calls are dropped as they arrive, and what they "
                + "held to write to you has been revoked. The conversation is untouched, and "
                + "writing to them here lifts the block."
        }

        Rectangle { visible: !root.saved; Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 4 }

        // Destructive chat actions. Clearing empties the history (the chat stays);
        // deleting removes the contact and the whole conversation for good.
        // Three labels with a drawing each do not fit one line of this panel, so
        // the one that only touches the history keeps a line to itself.
        ColumnLayout {
            visible: !root.saved
            Layout.fillWidth: true
            spacing: 8
            MenuButton {
                Layout.fillWidth: true
                iconName: "trash"
                text: "Clear chat"
                onClicked: clearChoiceDialog.open()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                MenuButton {
                    Layout.fillWidth: true
                    iconName: root.blocked ? "check" : "block"
                    text: root.blocked ? "Unblock" : "Block"
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
                    text: "Delete contact"
                    danger: true
                    onClicked: deleteContactDialog.open()
                }
            }
        }

        // Whether this account may write to them: whether it holds the pass they
        // issued. Nothing runs it down, so this is a yes or a no rather than a
        // number, and it is the same answer on every device of yours.
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 2
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            visible: !root.saved && root.session && root.session.activePeer.length > 0
            text: {
                // Named so the binding depends on it: the answer is read through a
                // call, and without something that changes when the contacts do,
                // it is whatever it was when this panel was built.
                const revision = root.session ? root.session.contactsRevision : 0
                const may = root.session ? root.session.canWriteTo(root.session.activePeer) : false
                return may ? "You can write to them"
                           : "They have not let this account write to them"
            }
        }
    }

    // Clear-chat choice: only your copy, or ask the peer to clear theirs too.
    // Emptying the saved chat is one action on every device of this account, so it
    // is confirmed rather than offered as a plain button.
    Dialog {
        id: clearSavedDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: "Clear saved messages"
            color: Theme.green
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Clear everywhere"
            rejectText: "Cancel"
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
            text: "Everything kept here goes, on this device and on every other device of "
                + "this account. There is no copy anywhere else."
        }
    }

    // Blocking says what it does and what it leaves alone, because it leaves the
    // conversation alone - which is not what the word usually promises.
    Dialog {
        id: blockDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(380, parent ? parent.width - 24 : 380)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: "Block this contact?"
            color: Theme.green
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Block"
            rejectText: "Cancel"
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
            text: "Their messages and calls stop arriving, and the pass they held to write "
                + "to you is revoked at your server. The conversation and its history stay "
                + "where they are; deleting them is a separate action. Writing to them again "
                + "lifts the block by itself, and they can answer straight away."
        }
    }

    Dialog {
        id: clearChoiceDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: "Clear chat"
            color: Theme.green
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Cancel"
            showReject: false
            onAccepted: clearChoiceDialog.close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: "Remove every message in this chat. The chat itself stays."
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "person"
                text: "Clear only for me"
                onClicked: {
                    if (root.session) { root.session.clearChat(false) }
                    clearChoiceDialog.close()
                    root.close()
                }
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "people"
                text: "Clear for everyone"
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
                text: "“For everyone” asks the other side to clear their copy too; their client clears it automatically."
            }
        }
    }

    // Irreversible contact deletion.
    Dialog {
        id: deleteContactDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
        header: Label {
            text: "Delete contact"
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Delete"
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
            text: "This permanently removes this contact and your entire chat history with them from this device. This cannot be undone."
        }
        }
    }
}
