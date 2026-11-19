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
    readonly property bool refused: root.session !== null && root.session.approvalRefused
    readonly property int failure: root.session !== null ? root.session.linkFailure : LinkStatus.None
    readonly property bool noNetwork: root.isConnecting && root.failure === LinkStatus.NoNetwork
    readonly property string whyNotReachable: root.session === null
        || root.failure === LinkStatus.NoNetwork || Tr.language.length < 0 ? ""
        : root.failure !== LinkStatus.None ? root.session.linkFailureText(root.failure)
        : I2p.transport === "gateway" ? qsTr("Reaching the external gateway")
        : I2p.transport === "sam" ? qsTr("Reaching the I2P router")
        : qsTr("The I2P router is joining the network")
    readonly property int kPadding: 4
    readonly property int kSideMargin: 12

    visible: root.session !== null && root.session.connected
        && (root.isOffline || root.isConnecting || root.heldForApproval || root.refused)
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
            text: root.refused ? qsTr("Not approved")
                : root.heldForApproval ? qsTr("Waiting for approval")
                : root.isOffline ? qsTr("Disconnected")
                : root.noNetwork ? qsTr("No network")
                : qsTr("Connecting…")
            color: Theme.warnInk
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
        Label {
            text: root.refused
                ? (root.session.approvalNote.length > 0
                    ? root.session.approvalNote
                    : qsTr("The server operator declined this account"))
                : root.heldForApproval
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
