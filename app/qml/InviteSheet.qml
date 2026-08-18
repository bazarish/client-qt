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
    // Return to the page this opened from (Settings); the close button exits.
    signal back()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 18
    onOpened: { uri = ""; unavailable = ""; if (session) session.requestInvite() }

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
            IconButton { text: "‹"; font.pixelSize: 26; onClicked: root.back() }
            Label { text: "My invite"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        Label {
            text: "Anyone with this can verify and reach you with no trust in any server."
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
                text: "Your server operates your destination on your behalf. Publishing hands it "
                    + "a time-boxed delegation; it can take a few minutes to come up."
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
                contentItem: Label {
                    text: parent.text; color: Theme.green
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // The invite is a compact descriptor, so it fits one QR: show the code
        // and the link side by side (scan or copy).
        QrView {
            Layout.alignment: Qt.AlignHCenter
            visible: root.uri.length > 0
            text: root.uri
        }
        ScrollView {
            visible: root.uri.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            TextArea {
                id: linkArea
                readOnly: true
                wrapMode: TextArea.WrapAnywhere
                text: root.uri
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
            }
        }
        Button {
            id: copyBtn
            hoverEnabled: true
            property bool copied: false
            Layout.fillWidth: true
            text: copied ? "Copied ✓" : "Copy link"
            enabled: root.uri.length > 0
            onClicked: {
                linkArea.selectAll(); linkArea.copy(); linkArea.deselect()
                copied = true; copiedTimer.restart()
            }
            background: Rectangle {
                radius: 10
                color: copyBtn.copied ? Theme.success : (copyBtn.enabled ? Theme.accent : Theme.surfaceAlt)
                Behavior on color { ColorAnimation { duration: 200 } }
            }
            contentItem: Label { text: copyBtn.text; color: copyBtn.enabled ? Theme.accentText : Theme.textDim; horizontalAlignment: Text.AlignHCenter }
            Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
        }
    }
}
