// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null

    function takeLink(uri) {
        connectForm.takeLink(uri)
    }

    IconButton {
        iconName: "back"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        onClicked: App.requestAddAccount()
    }

    SignWithKeySheet { id: signSheet; session: root.session; onBack: signSheet.close() }
    ConnectProgressDialog { id: connectDialog; session: root.session }

    IconButton {
        iconName: "gear"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        onClicked: appSettings.open()
    }
    RouterStatusPage { id: routerStatus; onBack: { routerStatus.close(); appSettings.open() } }
    AppSettingsPage {
        id: appSettings
        parent: Overlay.overlay
        anchors.centerIn: parent
        onShowRouterStatus: routerStatus.open()
    }

    ScrollColumn {
        z: -1
        anchors.fill: parent
        anchors.bottomMargin: signatureButton.height + 2 * signatureButton.anchors.bottomMargin
        centered: true
        columnWidth: Math.min(availableWidth - 2 * Theme.spacing, 480)
        spacing: 14

        Label {
            text: qsTr("Connect to a server")
            color: Theme.green
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            visible: root.session !== null && root.session.serverFingerprint.length > 0
                && root.session.configuredFacades.length === 0
            text: qsTr("The saved server address cannot be used.")
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            visible: root.session && root.session.fingerprint.length > 0
            Layout.fillWidth: true
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: Theme.border
            implicitHeight: idCol.implicitHeight + 16
            ColumnLayout {
                id: idCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Label {
                    text: root.session && root.session.displayName.length > 0
                        ? root.session.displayName : qsTr("This account")
                    color: Theme.text
                    font.weight: Font.Medium
                }
                Label {
                    text: root.session ? root.session.fingerprint : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }

        ServerConnectForm {
            id: connectForm
            Layout.fillWidth: true
            session: root.session
            actionText: qsTr("Connect")
            initialFacades: root.session ? root.session.configuredFacades : []
            initialFingerprint: root.session ? root.session.serverFingerprint : ""
            initialReseeds: root.session ? root.session.configuredReseeds : []
        }

        Label {
            visible: root.session && root.session.connecting && connectDialog.suppressed
            text: qsTr("Connecting (%1%)").arg(root.session ? root.session.connectPercent : 0)
            color: Theme.accent
            font.pixelSize: Theme.fontSmall
            Layout.alignment: Qt.AlignHCenter
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { connectDialog.suppressed = false; connectDialog.open() }
            }
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            visible: root.session && root.session.connectError.length > 0
            text: root.session ? root.session.connectError : ""
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    Button {
        id: signatureButton
        focusPolicy: Theme.pressFocus
        text: qsTr("Signature")
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 18
        padding: 8
        leftPadding: 14
        rightPadding: 14
        onClicked: signSheet.open()
        background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
        contentItem: IconLabel { name: "key"; color: Theme.accent }
    }
}
