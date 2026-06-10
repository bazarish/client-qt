import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Popup {
    id: root
    property var session: null
    signal showInvite()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(parent ? parent.height - 40 : 600, 640)
    padding: 0

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    property string pendingExportFile: ""

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label { text: "Settings"; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: root.width
                spacing: 14

                // Profile
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Profile"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    RowLayout {
                        spacing: 12
                        Avatar { fingerprint: root.session ? root.session.fingerprint : ""; size: 56 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: root.session ? root.session.displayName : ""; color: Theme.text; font.weight: Font.Medium }
                            Label { text: root.session ? root.session.shortFingerprint(root.session.fingerprint) : ""; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    Button { text: "Show my invite / QR"; onClicked: { root.close(); root.showInvite() } }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Username
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Username"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label { text: "Register a memorable name others can add you by."; color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        FormField { id: aliasField; label: ""; placeholder: "username" }
                        Button { text: "Register"; enabled: aliasField.text.trim().length > 0; onClicked: root.session.registerAlias(aliasField.text.trim()) }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Connection
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 4
                    Label { text: "Connection"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label { text: root.session && root.session.connected ? ("Connected · " + root.session.subscriptionText) : "Not connected"; color: Theme.text }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Backup
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Backup"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Button { text: "Export encrypted backup…"; onClicked: exportDialog.open() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Theme + session
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Dark theme"; color: Theme.text; Layout.fillWidth: true }
                        Switch { checked: Theme.dark; onToggled: Theme.dark = checked }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Send read receipts"; color: Theme.text }
                            Label { text: "Lets contacts see a green tick when you receive."; color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        Switch {
                            checked: root.session ? root.session.sendReceipts : true
                            onToggled: if (root.session) root.session.sendReceipts = checked
                        }
                    }
                    Button {
                        text: "Sign out"
                        onClicked: { root.close(); App.closeProfile() }
                        background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
                        contentItem: Label { text: parent.text; color: Theme.danger; horizontalAlignment: Text.AlignHCenter }
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        currentFile: "file:///bazarish-backup.baz"
        onAccepted: { root.pendingExportFile = selectedFile; exportPassDialog.open() }
    }
    Dialog {
        id: exportPassDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Backup password"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.session.exportProfile(root.pendingExportFile, exportPass.text)
        contentItem: TextField { id: exportPass; echoMode: TextInput.Password; placeholderText: "password"; implicitWidth: 260 }
    }
}
