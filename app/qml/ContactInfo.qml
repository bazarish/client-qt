import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 420
    padding: 18

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // Prefill the rename field with the current local name each time it opens.
    onOpened: nameField.text = (session ? session.contactName(session.activePeer) : "")

    function saveName() {
        if (session) {
            session.renameContact(session.activePeer, nameField.text)
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Contact"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }

        Avatar {
            Layout.alignment: Qt.AlignHCenter
            fingerprint: root.session ? root.session.activePeer : ""
            size: 88
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
        Label {
            text: "Scan to verify the contact's identity:"
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            Layout.alignment: Qt.AlignHCenter
        }
        QrView {
            Layout.alignment: Qt.AlignHCenter
            text: root.session ? root.session.activePeer : ""
            dim: 200
        }
        TextArea {
            Layout.fillWidth: true
            readOnly: true
            wrapMode: TextArea.WrapAnywhere
            text: root.session ? root.session.activePeer : ""
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            selectByMouse: true
            background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
        }
    }
}
