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
    // The gateway is only to blame when it is the part that is silent.
    readonly property string whyNotReachable: (I2p.gatewayEnabled && !I2p.ready)
        ? qsTr("No connection to the HTTPS gateway")
        : (I2p.tunnelsBuilding ? qsTr("The I2P tunnels are being built")
                               : qsTr("No tunnel to the server"))
    readonly property int transportPollMs: 2000
    readonly property int kPadding: 4
    readonly property int kSideMargin: 12

    visible: root.session !== null && root.session.connected
        && (root.isOffline || root.isConnecting || root.heldForApproval)
    implicitHeight: line.implicitHeight + 2 * root.kPadding
    color: Theme.warn

    // The router reports nothing by itself: I2p holds what the last refresh read.
    Timer {
        interval: root.transportPollMs
        repeat: true
        running: root.visible && root.isConnecting
        triggeredOnStart: true
        onTriggered: I2p.refresh()
    }

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
                : (root.isOffline ? qsTr("Offline") : qsTr("Connecting…"))
            color: Theme.accentInk
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
        Label {
            text: root.heldForApproval
                ? (root.session.approvalNote.length > 0
                    ? root.session.approvalNote
                    : qsTr("Your server holds this account until its operator approves it — you cannot send or receive yet"))
                : (root.isOffline ? qsTr("This account is not syncing") : root.whyNotReachable)
            color: Theme.accentInk
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
    TapHandler { onTapped: root.tapped() }
}
