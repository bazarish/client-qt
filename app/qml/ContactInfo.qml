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
            Label { text: "Contact"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

        Avatar {
            Layout.alignment: Qt.AlignHCenter
            fingerprint: root.session ? root.session.activePeer : ""
            size: 88
            enlargeable: true
        }
        // The contact's display name (the local label, or a short fingerprint).
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.session ? root.session.peerName(root.session.activePeer) : ""
            color: Theme.text
            font.weight: Font.Medium
            font.pixelSize: Theme.fontTitle
        }
        // Rename: a purely local label, mirrored only to your own other devices -
        // the contact is never told the name you keep them under.
        Label {
            text: "Display name (local only):"
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }
        RowLayout {
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
            MenuButton { text: "Save"; onClicked: root.saveName() }
        }
        // The identity itself, on one line: the middle gives way when it does not
        // fit, and a tap anywhere on it copies the whole thing.
        RowLayout {
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

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 4 }


        // The label stays short in every state - a button is not the place for a
        // sentence; the reason is written under it.
        MenuButton {
            Layout.fillWidth: true
            enabled: root.shareLink.length > 0
            text: root.shareLink.length > 0
                ? "Share contact…"
                : (root.sharingRefused ? "Sharing is off" : "Not shareable yet")
            onClicked: { root.close(); root.shareRequested() }
        }
        // Said under the button, not on it: the reason is a sentence, and a
        // button wearing one cuts it off in the middle.
        Label {
            visible: root.shareLink.length === 0
            text: root.sharingRefused
                ? "This contact has turned off being passed on."
                : "Their descriptor arrives with their next message."
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 4 }

        // Destructive chat actions. Clearing empties the history (the chat stays);
        // deleting removes the contact and the whole conversation for good.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            MenuButton {
                Layout.fillWidth: true
                text: "Clear chat"
                onClicked: clearChoiceDialog.open()
            }
            MenuButton {
                Layout.fillWidth: true
                text: "Delete contact"
                danger: true
                onClicked: deleteContactDialog.open()
            }
        }

        // Sending capacity: one-time tickets this device holds for them. They
        // hand over a batch, each message spends one, and the client asks for
        // more before running out - so this is a number to glance at, not to act
        // on. It is per device: another device of yours holds its own.
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 2
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            visible: root.session && root.session.activePeer.length > 0
            text: {
                const n = root.session ? root.session.sendCapacity(root.session.activePeer) : 0
                return n === 1 ? "1 send ticket left on this device"
                               : n + " send tickets on this device"
            }
        }
    }

    // Clear-chat choice: only your copy, or ask the peer to clear theirs too.
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
                text: "Clear only for me"
                onClicked: {
                    if (root.session) { root.session.clearChat(false) }
                    clearChoiceDialog.close()
                    root.close()
                }
            }
            MenuButton {
                Layout.fillWidth: true
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
