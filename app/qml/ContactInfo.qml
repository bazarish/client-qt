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

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Contact"; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }

        Avatar {
            Layout.alignment: Qt.AlignHCenter
            fingerprint: root.session ? root.session.activePeer : ""
            size: 88
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.session ? root.session.shortFingerprint(root.session.activePeer) : ""
            color: Theme.text
            font.weight: Font.Medium
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
