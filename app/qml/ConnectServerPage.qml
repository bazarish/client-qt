import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null

    // Back to the profile list (no server needed to switch/create a profile).
    IconButton {
        iconName: "back"
        font.pixelSize: 26
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        onClicked: App.requestAddAccount()
    }

    SignWithKeySheet { id: signSheet; session: root.session }
    // Connecting is minutes of real work over I2P: show it, with steps.
    ConnectProgressDialog { id: connectDialog; session: root.session }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 480)
        spacing: 14

        Label {
            text: "Connect to a server"
            color: Theme.green
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Your profile needs a serving server to send and receive. "
                + "Everything stays end-to-end encrypted; a facade is just the last mile."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Which profile is being connected: name and fingerprint.
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
                        ? root.session.displayName : "This profile"
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

        // The descriptor form (link paste or manual facades + fingerprint). The
        // user subscribes on the server's portal; this only connects.
        ServerConnectForm {
            Layout.fillWidth: true
            session: root.session
            actionText: "Connect"
        }

        // The dialog can be hidden while the connect runs; this is the way back.
        Label {
            visible: root.session && root.session.connecting && connectDialog.suppressed
            text: "Connecting (" + (root.session ? root.session.connectPercent : 0) + "%) — show progress"
            color: Theme.accent
            font.pixelSize: Theme.fontSmall
            Layout.alignment: Qt.AlignHCenter
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { connectDialog.suppressed = false; connectDialog.open() }
            }
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

    // Footer: the key is the user's sign-in to every portal and any
    // sign-in-with-key site, available with or without a server.
    Button {
        text: "Signature"
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 18
        padding: 8
        onClicked: signSheet.open()
        background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
        contentItem: Label {
            text: parent.text; color: Theme.accent
            leftPadding: 14; rightPadding: 14
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
    }
}
