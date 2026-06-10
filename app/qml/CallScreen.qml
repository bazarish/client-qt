import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 360
    height: 420
    padding: 18

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 16
        Item { Layout.fillHeight: true }
        Avatar {
            Layout.alignment: Qt.AlignHCenter
            fingerprint: root.session ? root.session.activePeer : ""
            size: 120
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.session ? root.session.shortFingerprint(root.session.activePeer) : ""
            color: Theme.text
            font.pixelSize: Theme.fontTitle
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: "Call signalling is wired (call.invite/accept/decline/end). "
                + "Live audio/video over the I2P tunnel is coming."
            color: Theme.textDim
        }
        Item { Layout.fillHeight: true }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "End"
            onClicked: root.close()
            background: Rectangle { radius: 24; color: Theme.danger; implicitWidth: 120; implicitHeight: 48 }
            contentItem: Label { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
    }
}
