// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Rectangle {
    id: root
    property var session: null
    signal tapped()

    readonly property bool isOffline: root.session !== null && !root.session.online
    readonly property bool isConnecting: root.session !== null && root.session.online
        && !root.session.reachable
    readonly property bool heldForApproval: root.session !== null && root.session.awaitingApproval
    readonly property int failure: root.session !== null ? root.session.linkFailure : LinkStatus.None
    readonly property bool noNetwork: root.isConnecting && root.failure === LinkStatus.NoNetwork
    readonly property string whyNotReachable: {
        switch (root.failure) {
        case LinkStatus.I2pUnreachable:
            return qsTr("No connection to the I2P network")
        case LinkStatus.GatewayUnreachable:
            return qsTr("No connection to the external gateway")
        case LinkStatus.GatewayRefused:
            return qsTr("The gateway refused a link to the server")
        case LinkStatus.TunnelsBuilding:
            return qsTr("The I2P tunnels are being built")
        case LinkStatus.ServerUnreachable:
            return qsTr("Could not connect to the server")
        case LinkStatus.ServerDown:
            return qsTr("No tunnel to the server")
        case LinkStatus.ServerRefused:
            return qsTr("The server refused the request")
        }
        return ""
    }
    readonly property int kPadding: 4
    readonly property int kSideMargin: 12

    visible: root.session !== null && root.session.connected
        && (root.isOffline || root.isConnecting || root.heldForApproval)
    implicitHeight: line.implicitHeight + 2 * root.kPadding
    color: Theme.warnFill

    RowLayout {
        id: line
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.kSideMargin
        anchors.rightMargin: root.kSideMargin
        spacing: 8
        Label {
            text: root.heldForApproval ? qsTr("Waiting for approval")
                : root.isOffline ? qsTr("Disconnected")
                : root.noNetwork ? qsTr("No network")
                : qsTr("Connecting…")
            color: Theme.warnInk
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
        Label {
            text: root.heldForApproval
                ? (root.session.approvalNote.length > 0
                    ? root.session.approvalNote
                    : qsTr("Your server holds this account until its operator approves it — you cannot send or receive yet"))
                : (root.isOffline ? qsTr("This account is not syncing") : root.whyNotReachable)
            color: Theme.warnInk
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
    TapHandler { onTapped: root.tapped() }
}
