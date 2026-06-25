import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    property string uri: ""
    // Return to the page this opened from (Settings); the close button exits.
    signal back()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 18
    onOpened: { uri = ""; if (session) session.requestInvite() }

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onInviteReady(u) { root.uri = u }
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
        // The invite is a compact descriptor, so it fits one QR: show the code
        // and the link side by side (scan or copy).
        QrView {
            Layout.alignment: Qt.AlignHCenter
            visible: root.uri.length > 0
            text: root.uri
        }
        ScrollView {
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
