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
    property bool showQr: false
    readonly property int kLinkLines: 4
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
        showQr = false
        // Only when there is nothing stored is anything asked of the server: that
        // means the card never picked up the serving key.
        if (session && uri.length === 0) {
            session.requestInvite()
        }
    }

    background: DialogFrame { }

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
            IconButton { iconName: "back"; onClicked: root.back() }
            Label { text: qsTr("My invite"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton {
                iconName: "qr"
                visible: root.hasAliases && root.hasInvite
                tint: root.showQr ? Theme.accent : Theme.textDim
                onClicked: root.showQr = !root.showQr
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Label {
            text: root.hasAliases
                ? qsTr("You can be added as a contact by alias or by the full descriptor.")
                : qsTr("Anyone with the link can reach you.")
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
                text: qsTr("No invite yet: %1.").arg(root.unavailable)
                color: Theme.warn
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("An invite cannot be formed before your destination is up. Publishing takes a few minutes.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Publish my destination")
                Layout.fillWidth: true
                onClicked: if (root.session) { root.session.publishPersonalDest(); root.unavailable = qsTr("Publishing. This takes a few minutes") }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.green }
                contentItem: IconLabel { name: "broadcast"; color: Theme.green }
            }
        }

        // One alias per row: the name, the day it runs out, and a switch that
        // decides whether it answers at all. A tap on the name puts it on the
        // clipboard, the way every other thing here that is meant to be handed
        // over is copied. Which names are listed is settled on the website.
        ColumnLayout {
            visible: root.hasAliases && !root.showQr
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
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (!root.session) {
                                    return
                                }
                                App.copyText("!" + aliasRow.modelData.alias)
                                aliasRow.copied = true
                                aliasCopied.restart()
                            }
                        }
                        Label {
                            text: "!"
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontLarge
                        }
                        Label {
                            text: aliasRow.modelData.alias
                            color: aliasRow.copied
                                ? Theme.green
                                : (aliasRow.modelData.resolving ? Theme.text : Theme.textDim)
                            font.pixelSize: Theme.fontLarge
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        text: aliasRow.copied
                            ? qsTr("Copied to clipboard")
                            : (aliasRow.modelData.resolving
                                ? (aliasRow.modelData.live
                                    ? aliasRow.modelData.term
                                    : qsTr("pointing it here…"))
                                : qsTr("answers nobody"))
                        color: aliasRow.copied ? Theme.green : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                    Toggle {
                        checked: aliasRow.modelData.resolving
                        enabled: root.session !== null && root.session.connected
                            && !root.session.aliasBusy
                        onToggled: root.session.setAliasBinding(
                            aliasRow.modelData.alias, checked)
                    }
                }
            }
        }
        Label {
            visible: root.hasAliases && !root.showQr && root.aliasNote.length > 0
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
            visible: root.hasInvite && (!root.hasAliases || root.showQr)
            text: root.uri
        }
        FontMetrics { id: linkMetrics; font: linkArea.font }
        // The box stays even with nothing in it: an invite that is not ready yet
        // is a state to explain, not a control to make disappear.
        ScrollView {
            visible: !root.hasAliases
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasInvite
                ? linkArea.topPadding + linkArea.bottomPadding
                    + root.kLinkLines * Math.ceil(linkMetrics.lineSpacing)
                : 56
            TextArea {
                id: linkArea
                readOnly: true
                wrapMode: TextArea.WrapAnywhere
                text: root.uri
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: linkArea.leftPadding
                    anchors.rightMargin: linkArea.rightPadding
                    anchors.topMargin: linkArea.topPadding
                    visible: linkArea.length === 0 && root.unavailable.length === 0
                    text: qsTr("Waiting for the server…")
                    color: Theme.textDim
                    wrapMode: Text.Wrap
                }
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
                text: copied ? qsTr("Copied") : qsTr("Copy link")
                enabled: root.hasInvite
                // Copied from what the sheet holds, not from the box: the box is
                // not on screen once there are aliases to show instead.
                onClicked: {
                    App.copyText(root.uri)
                    copied = true; copiedTimer.restart()
                }
                background: Rectangle {
                    radius: 10
                    color: copyBtn.copied ? Theme.success : (copyBtn.enabled ? Theme.accent : Theme.surfaceAlt)
                    Behavior on color { ColorAnimation { duration: 200 } }
                }
                contentItem: IconLabel {
                    name: "copy"
                    color: copyBtn.enabled ? Theme.accentText : Theme.textDim
                }
                Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
            }
            // Nothing is asked of the registry until this is pressed; after that
            // the client keeps the aliases their owner pointed here pointing
            // here, and leaves the rest alone.
            MenuButton {
                iconName: "bang"
                text: root.session && root.session.aliasBusy
                    ? qsTr("Asking…")
                    : qsTr("Check my aliases")
                enabled: root.session && root.session.connected && !root.session.aliasBusy
                onClicked: root.session.activateAliasServicing()
            }
        }
    }
}
