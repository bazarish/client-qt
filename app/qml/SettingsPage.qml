import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

Popup {
    id: root
    property var session: null
    signal showInvite()
    signal showSignWithKey()
    signal showRouterStatus()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(parent ? parent.height - 40 : 600, 640)
    padding: 0

    // Refresh the per-user I2P destination status whenever Settings opens.
    onOpened: if (session) session.refreshI2pStatus()

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    property string pendingExportFile: ""

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label { text: "Settings"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
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
                        // Tap the avatar (or the button below) to set a photo.
                        Avatar {
                            fingerprint: root.session ? root.session.fingerprint : ""
                            size: 56
                            TapHandler { onTapped: avatarDialog.open() }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: root.session ? root.session.displayName : ""; color: Theme.text; font.weight: Font.Medium }
                            Label { text: root.session ? root.session.shortFingerprint(root.session.fingerprint) : ""; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    MenuButton { Layout.fillWidth: true; text: "Set photo…"; onClicked: avatarDialog.open() }
                    MenuButton { Layout.fillWidth: true; text: "Show my invite / QR"; onClicked: { root.close(); root.showInvite() } }
                    MenuButton { Layout.fillWidth: true; text: "Sign in with key (portals / sites)"; onClicked: { root.close(); root.showSignWithKey() } }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Connection
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 6
                    Label { text: "Server connection"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label {
                        text: !(root.session && root.session.connected)
                            ? "No server configured"
                            : (root.session.reachable
                                ? "Connected"
                                : "Not reaching the server — Connect to finish setup")
                        color: (root.session && root.session.connected && !root.session.reachable)
                            ? Theme.warn : Theme.text
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        visible: root.session && root.session.connected
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 8; implicitHeight: 8; radius: 4
                            color: (root.session && root.session.reachable) ? Theme.success : Theme.warn
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.session && root.session.activeFacade.length > 0
                                ? ((root.session.reachable ? "via " : "connecting via ") + root.session.activeFacade)
                                : ""
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle
                        }
                    }
                    Label {
                        visible: root.session && root.session.configuredFacades.length > 1
                        text: root.session ? (root.session.configuredFacades.length + " facades configured (failover)") : ""
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        // Full descriptor editor (link / facades / fingerprint), the
                        // same flow as first connect: lets the user re-point the server
                        // or, after registering on the portal, Connect again to finish.
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Server connection…"
                            onClicked: connectionDialog.open()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            text: "I2P router & status…"
                            onClicked: { root.close(); root.showRouterStatus() }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Personal I2P destination
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Personal I2P destination"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label {
                        text: "Off by default, you share a fixed address from the server pool. Turn this on to be served on your own stable destination — portable across servers, kept even if you move. Trade-off: a unique, lasting address across all your contacts (less crowd-blending than the shared pool). Billed per term, renewed from your balance."
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    // Status block. "Refresh" re-polls the server and briefly tints
                    // this box so the user sees the data was just updated; the
                    // term line reads "Inactive" whenever the subscription is not
                    // currently paid-active.
                    Rectangle {
                        id: i2pStatusBox
                        Layout.fillWidth: true
                        radius: Theme.radiusSmall
                        color: "transparent"
                        implicitHeight: i2pStatusCol.implicitHeight + 12
                        SequentialAnimation {
                            id: i2pFlash
                            PropertyAction { target: i2pStatusBox; property: "color"; value: Qt.rgba(0.12, 0.48, 0.08, 0.5) }
                            PauseAnimation { duration: 550 }
                            ColorAnimation { target: i2pStatusBox; property: "color"; to: "transparent"; duration: 500 }
                        }
                        ColumnLayout {
                            id: i2pStatusCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 6
                            spacing: 4
                            Label {
                                text: root.session ? root.session.i2pStatusText : ""
                                color: (root.session && root.session.i2pActive) ? Theme.success : Theme.text
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            Label {
                                text: root.session && root.session.i2pActive && root.session.i2pPaidThrough > 0
                                    ? ("Active until " + Qt.formatDate(new Date(root.session.i2pPaidThrough * 1000), "yyyy-MM-dd"))
                                    : "Inactive"
                                color: (root.session && root.session.i2pActive && root.session.i2pPaidThrough > 0)
                                    ? Theme.success : Theme.warn
                                font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                visible: root.session && root.session.i2pAddress.length > 0
                                text: root.session ? root.session.i2pAddress : ""
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                        }
                    }
                    // Set up a master key first (generate or load a .dat), then turn it on.
                    RowLayout {
                        visible: root.session && !root.session.i2pHasKey
                        Layout.fillWidth: true; spacing: 8
                        MenuButton { Layout.fillWidth: true; text: "Generate key"; onClicked: root.session.generatePersonalKey() }
                        MenuButton { Layout.fillWidth: true; text: "Load .dat…"; onClicked: i2pKeyDialog.open() }
                    }
                    RowLayout {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true; spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && !root.session.i2pEnabled
                            text: "Turn on"
                            enabled: root.session && root.session.connected
                            onClicked: root.session.enablePersonalDest()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && root.session.i2pEnabled
                            text: "Turn off"
                            onClicked: root.session.disablePersonalDest()
                        }
                        // Re-poll the server status and flash the box for ~1s.
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Refresh"
                            onClicked: { if (root.session) root.session.refreshI2pStatus(); i2pFlash.restart() }
                        }
                    }
                    // Permanently drop the personal master key (reverts to the pool).
                    MenuButton {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true
                        text: "Delete key"
                        danger: true
                        onClicked: deleteKeyDialog.open()
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Backup
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Backup"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    MenuButton { Layout.fillWidth: true; text: "Export encrypted backup…"; onClicked: exportDialog.open() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Session
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
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
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Sign out"
                            onClicked: { root.close(); App.closeProfile() }
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Delete account…"
                            danger: true
                            onClicked: deleteDialog.open()
                        }
                    }
                }
            }
        }
    }

    // The whole profile (keys, routing meta and contacts) exports to one
    // password-protected <username>.bazarish file; the same file restores it.
    property string backupName: (root.session && root.session.displayName.length > 0
        ? root.session.displayName.replace(/[^A-Za-z0-9._-]+/g, "_") : "bazarish")
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "bazarish"
        nameFilters: ["Bazarish backup (*.bazarish)", "All files (*)"]
        currentFile: "file:///" + root.backupName + ".bazarish"
        onAccepted: { root.pendingExportFile = selectedFile; exportPassDialog.open() }
    }
    FileDialog {
        id: i2pKeyDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["I2P destination key (*.dat)", "All files (*)"]
        onAccepted: if (root.session) root.session.loadPersonalKey(selectedFile)
    }
    // Pick an image for the profile photo; the app squares and compresses it to
    // within the 500 KB protocol cap before storing and distributing it.
    FileDialog {
        id: avatarDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: if (root.session) root.session.setAvatar(selectedFile)
    }
    Dialog {
        id: exportPassDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Backup password"
        onAccepted: root.session.exportProfile(root.pendingExportFile, exportPass.text)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Backup password"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { onAccepted: exportPassDialog.accept(); onRejected: exportPassDialog.reject() }
        contentItem: TextField { id: exportPass; echoMode: TextInput.Password; placeholderText: "password"; color: Theme.text; placeholderTextColor: Theme.textDim; implicitWidth: 260; onAccepted: exportPassDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: exportPass.activeFocus ? Theme.accent : Theme.border } }
    }

    // Full server-connection editor: the same descriptor flow as first connect,
    // reachable any time so a profile stuck unconnected can be repaired (re-point
    // the server, or Connect again once registered on the portal). The form
    // carries its own Connect action, so the dialog has no footer.
    Dialog {
        id: connectionDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 460
        onOpened: connectionForm.reset()
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: RowLayout {
            spacing: 4
            IconButton { text: "‹"; font.pixelSize: 26; Layout.leftMargin: 8; onClicked: connectionDialog.close() }
            Label {
                text: "Server connection"
                color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
                Layout.fillWidth: true; Layout.rightMargin: 14; topPadding: 14; bottomPadding: 14
            }
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: "Paste a server link, or edit the facades and fingerprint, then "
                    + "Connect. If the server says your key needs registration, "
                    + "register on its portal and Connect again to finish setup."
                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            ServerConnectForm {
                id: connectionForm
                Layout.fillWidth: true
                session: root.session
                actionText: "Connect"
                initialFacades: root.session ? root.session.configuredFacades : []
                initialFingerprint: root.session ? root.session.serverFingerprint : ""
                onSubmitted: connectionDialog.close()
            }
        }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 360
        title: "Delete account"
        footer: DialogButtons { acceptText: "Delete"; danger: true; onAccepted: deleteDialog.accept(); onRejected: deleteDialog.reject() }
        onAccepted: {
            const id = root.session ? root.session.accountId : ""
            root.close()
            if (id.length > 0) {
                App.deleteProfile(id)
            }
        }
        // Exceptional/destructive: brightest-neon outline so it is unmistakable,
        // dark surface with light text so it is actually readable.
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
        header: Label { text: "Delete account"; color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        contentItem: Label {
            text: "Permanently delete this account and all its messages from this "
                + "device? Make sure you have a backup if you might need it again. "
                + "This cannot be undone."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: deleteKeyDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 360
        title: "Delete personal I2P key"
        footer: DialogButtons { acceptText: "Delete"; danger: true; onAccepted: deleteKeyDialog.accept(); onRejected: deleteKeyDialog.reject() }
        onAccepted: if (root.session) root.session.deletePersonalKey()
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
        header: Label { text: "Delete personal I2P key"; color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        contentItem: Label {
            text: "The old key will be permanently deleted and cannot be recovered. "
                + "You will fall back to the shared pool address; enabling a personal "
                + "destination again later would create a new, different address."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
}
