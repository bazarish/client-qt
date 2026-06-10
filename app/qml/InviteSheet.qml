import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    property string uri: ""

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
            Label { text: "My invite"; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        Label {
            text: "Anyone with this can verify and reach you with no trust in any server."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: "The full chain is too large for a single QR — share the link."
            color: Theme.textDim
            font.italic: true
            font.pixelSize: Theme.fontSmall
            visible: root.uri.length > 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
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
            Layout.fillWidth: true
            text: "Copy link"
            enabled: root.uri.length > 0
            onClicked: { linkArea.selectAll(); linkArea.copy(); linkArea.deselect() }
            background: Rectangle { radius: 10; color: parent.enabled ? Theme.accent : Theme.surfaceAlt }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
