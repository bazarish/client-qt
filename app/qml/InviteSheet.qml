import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    property string uri: ""
    // Why there is no invite yet, when the routing is not published.
    property string unavailable: ""
    readonly property bool hasInvite: uri.length > 0
    // The names this account holds, as the registry last answered. Known ones
    // take the place of the code and the descriptor box: an alias is the short
    // thing to hand over, and the long form is still one button away.
    readonly property var aliasRows: session ? session.aliasHoldings : []
    readonly property string aliasNote: session ? session.aliasNote : ""
    readonly property bool hasAliases: aliasRows.length > 0
    // Return to the page this opened from (Settings); the close button exits.
    signal back()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    padding: 18
    onOpened: {
        // Straight from what this account stores - no request, works offline.
        uri = session ? session.ownInvite : ""
        unavailable = ""
        // Only when there is nothing stored is anything asked of the server: that
        // means the card never picked up the serving key.
        if (session && uri.length === 0) {
            session.requestInvite()
        }
    }

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onInviteReady(u) { root.uri = u; root.unavailable = "" }
        function onInviteUnavailable(reason) { root.uri = ""; root.unavailable = reason }
        // Publishing takes minutes and reports through the destination status:
        // retry the invite on every status change until there is one to show.
        function onI2pStatusChanged() {
            if (root.unavailable.length > 0 && root.session) {
                root.session.requestInvite()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            IconButton { iconName: "back"; font.pixelSize: 26; onClicked: root.back() }
            Label { text: "My invite"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Label {
            text: root.hasAliases
                ? "Give somebody one of these and they can reach you. Copy link still hands "
                    + "over the full descriptor."
                : "Anyone with this can verify and reach you with no trust in any server."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        // An invite with no routing in it would not be reachable, so say what is
        // missing and offer the one action that fixes it, instead of a blank box.
        ColumnLayout {
            visible: root.unavailable.length > 0
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "No invite yet: " + root.unavailable + "."
                color: Theme.warn
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: "An invite carries where to reach you and the key your server answers card "
                    + "fetches with, so it cannot be formed before your destination is up. "
                    + "Publishing hands your server a time-boxed delegation; it takes a few minutes."
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                text: "Publish my destination"
                Layout.fillWidth: true
                onClicked: if (root.session) { root.session.publishPersonalDest(); root.unavailable = "Publishing — this can take a few minutes" }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.green }
                contentItem: RowLayout {
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    Icon { name: "broadcast"; color: Theme.green; size: 15 }
                    Label { text: "Publish my destination"; color: Theme.green }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        // One alias per row: the name, and the day it runs out at the far edge.
        // A tap puts the name on the clipboard, the way every other thing here
        // that is meant to be handed over is copied. Only names that point at
        // this identity are here; the rest reach nobody.
        ColumnLayout {
            visible: root.hasAliases
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.aliasRows
                delegate: RowLayout {
                    id: aliasRow
                    required property var modelData
                    property bool copied: false
                    Layout.fillWidth: true
                    spacing: 8
                    Timer { id: aliasCopied; interval: 1500; onTriggered: aliasRow.copied = false }
                    Label {
                        text: "!" + aliasRow.modelData.alias
                        color: aliasRow.copied ? Theme.green : Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (!root.session) {
                                    return
                                }
                                root.session.copyText("!" + aliasRow.modelData.alias)
                                aliasRow.copied = true
                                aliasCopied.restart()
                            }
                        }
                    }
                    Label {
                        text: aliasRow.copied ? "Copied to clipboard" : aliasRow.modelData.term
                        color: aliasRow.copied ? Theme.green : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
        Label {
            visible: root.hasAliases && root.aliasNote.length > 0
            Layout.fillWidth: true
            text: root.aliasNote
            color: Theme.warn
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        // The invite is a compact descriptor, so it fits one QR: show the code
        // and the link side by side (scan or copy).
        QrView {
            Layout.alignment: Qt.AlignHCenter
            visible: root.hasInvite && !root.hasAliases
            text: root.uri
        }
        // The box stays even with nothing in it: an invite that is not ready yet
        // is a state to explain, not a control to make disappear.
        ScrollView {
            visible: !root.hasAliases
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasInvite ? 110 : 56
            TextArea {
                id: linkArea
                readOnly: true
                wrapMode: TextArea.WrapAnywhere
                text: root.uri
                placeholderText: "No invite yet — publish your destination first."
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                id: copyBtn
                hoverEnabled: true
                property bool copied: false
                Layout.fillWidth: true
                text: copied ? "Copied" : "Copy link"
                enabled: root.hasInvite
                // Copied from what the sheet holds, not from the box: the box is
                // not on screen once there are aliases to show instead.
                onClicked: {
                    root.session.copyText(root.uri)
                    copied = true; copiedTimer.restart()
                }
                background: Rectangle {
                    radius: 10
                    color: copyBtn.copied ? Theme.success : (copyBtn.enabled ? Theme.accent : Theme.surfaceAlt)
                    Behavior on color { ColorAnimation { duration: 200 } }
                }
                contentItem: RowLayout {
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    Icon { name: "copy"; color: copyBtn.enabled ? Theme.accentText : Theme.textDim; size: 15 }
                    Label { text: copyBtn.text; color: copyBtn.enabled ? Theme.accentText : Theme.textDim }
                    Item { Layout.fillWidth: true }
                }
                Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
            }
            // Nothing is asked of the registry until this is pressed; after that
            // the client keeps the aliases their owner pointed here pointing
            // here, and leaves the rest alone.
            MenuButton {
                iconName: "bang"
                text: root.session && root.session.aliasBusy
                    ? "Asking…"
                    : "Check my aliases"
                enabled: root.session && root.session.connected && !root.session.aliasBusy
                onClicked: root.session.activateAliasServicing()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 6 }

        // The way out of an invite that got somewhere it should not have: the
        // server's key for this account is replaced, which retires every link
        // already handed out.
        MenuButton {
            Layout.fillWidth: true
            iconName: "key"
            text: "Change the server key"
            danger: true
            enabled: root.session && root.session.connected && !root.session.servingKeyBusy
            onClicked: rotateDialog.open()
        }
    }

    Dialog {
        id: rotateDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(420, parent ? parent.width - 24 : 420)
        closePolicy: root.session && root.session.servingKeyBusy
            ? Popup.NoAutoClose : Popup.CloseOnEscape
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: "Change the server key"
            color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Change it"
            danger: true
            acceptEnabled: root.session && !root.session.servingKeyBusy
            onAccepted: root.session.rotateServingKey()
            onRejected: rotateDialog.close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "Your server holds a key that every message to you is sealed to, and "
                    + "your invite carries the capability that reads your card. Both are "
                    + "replaced here."
                color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label {
                text: "Every link you have handed out stops working, and anyone holding your "
                    + "old card can no longer deliver to you. Your current contacts are sent "
                    + "the new pair straight away; one that is offline picks it up from your "
                    + "next message to them."
                color: Theme.warn; font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            // What the rotation is doing, step by step: it talks to the server
            // twice and then to every contact, and a silent dialog through that
            // is a dialog that looks stuck.
            Label {
                visible: root.session && root.session.servingKeyStage.length > 0
                text: root.session ? root.session.servingKeyStage : ""
                color: (root.session && root.session.servingKeyBusy) ? Theme.accent : Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "Let contacts pass my contact on"; color: Theme.text }
                    Label {
                        text: "On, a contact can hand you to someone else. Off, they are sent "
                            + "no capability and their Share button says so."
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                }
                Toggle {
                    checked: root.session ? root.session.sharingAllowed : true
                    onToggled: if (root.session) { root.session.sharingAllowed = checked }
                }
            }
        }
    }
}
