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

    // Refresh the per-user I2P destination status whenever Settings opens.
    onOpened: if (session) session.refreshI2pStatus()

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
                    spacing: 6
                    Label { text: "Connection (HTTP facade)"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label {
                        text: root.session && root.session.connected ? ("Connected · " + root.session.subscriptionText) : "Not connected"
                        color: Theme.text
                    }
                    RowLayout {
                        visible: root.session && root.session.connected
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 8; implicitHeight: 8; radius: 4
                            color: (root.session && root.session.reachable) ? Theme.success : "#d4a017"
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
                        Button {
                            visible: root.session && root.session.connected
                            text: "Edit facades…"
                            onClicked: {
                                facadeModel.clear()
                                var cfg = root.session.configuredFacades
                                for (var i = 0; i < cfg.length; ++i) facadeModel.append({ url: cfg[i] })
                                if (facadeModel.count === 0) facadeModel.append({ url: "" })
                                facadeDialog.open()
                            }
                        }
                        Button {
                            visible: root.session && root.session.connected
                            text: "Share server link / QR"
                            onClicked: serverLinkDialog.open()
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
                    Label {
                        text: root.session ? root.session.i2pStatusText : ""
                        color: (root.session && root.session.i2pActive) ? Theme.success : Theme.text
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Label {
                        visible: root.session && root.session.i2pAddress.length > 0
                        text: root.session ? root.session.i2pAddress : ""
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle; Layout.fillWidth: true
                    }
                    // Set up a master key first (generate or load a .dat), then turn it on.
                    RowLayout {
                        visible: root.session && !root.session.i2pHasKey
                        Layout.fillWidth: true; spacing: 8
                        Button { text: "Generate key"; onClicked: root.session.generatePersonalKey() }
                        Button { text: "Load .dat…"; onClicked: i2pKeyDialog.open() }
                    }
                    RowLayout {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true; spacing: 8
                        Button {
                            visible: root.session && !root.session.i2pEnabled
                            text: "Turn on"
                            enabled: root.session && root.session.connected
                            onClicked: root.session.enablePersonalDest()
                        }
                        Button {
                            visible: root.session && root.session.i2pEnabled
                            text: "Turn off"
                            onClicked: root.session.disablePersonalDest()
                        }
                        Button { text: "Refresh"; onClicked: root.session.refreshI2pStatus() }
                    }
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
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            Layout.fillWidth: true
                            text: "Sign out"
                            onClicked: { root.close(); App.closeProfile() }
                            background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
                            contentItem: Label { text: parent.text; color: Theme.text; horizontalAlignment: Text.AlignHCenter }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Delete account…"
                            onClicked: deleteDialog.open()
                            background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.danger }
                            contentItem: Label { text: parent.text; color: Theme.danger; horizontalAlignment: Text.AlignHCenter }
                        }
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
    FileDialog {
        id: i2pKeyDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["I2P destination key (*.dat)", "All files (*)"]
        onAccepted: if (root.session) root.session.loadPersonalKey(selectedFile)
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

    Dialog {
        id: serverLinkDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 360
        title: "Share this server"
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "Anyone can add this server — fingerprint and facades — from this link or QR, with no manual entry."
                color: Theme.textDim; wrapMode: Text.Wrap
            }
            QrView { Layout.alignment: Qt.AlignHCenter; text: root.session ? root.session.myServerLink() : "" }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                TextArea {
                    readOnly: true
                    wrapMode: TextArea.WrapAnywhere
                    color: Theme.text
                    selectByMouse: true
                    text: root.session ? root.session.myServerLink() : ""
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                }
            }
        }
    }

    ListModel { id: facadeModel }
    Dialog {
        id: facadeDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 440
        title: "Facades (tried in order, with failover)"
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: {
            var urls = []
            for (var i = 0; i < facadeModel.count; ++i) {
                var u = facadeModel.get(i).url.trim()
                if (u.length > 0) urls.push(u)
            }
            if (urls.length > 0) root.session.updateFacades(urls)
        }
        contentItem: ColumnLayout {
            spacing: 8
            Repeater {
                model: facadeModel
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        Layout.fillWidth: true
                        text: model.url
                        placeholderText: "http[s]://host:port/secret-path"
                        color: Theme.text
                        selectByMouse: true
                        onTextChanged: facadeModel.setProperty(index, "url", text)
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    }
                    IconButton { text: "✕"; visible: facadeModel.count > 1; onClicked: facadeModel.remove(index) }
                }
            }
            Button {
                text: "＋ Add facade"
                onClicked: facadeModel.append({ url: "" })
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                contentItem: Label { text: parent.text; color: Theme.accent; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 360
        title: "Delete account"
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: {
            const id = root.session ? root.session.accountId : ""
            root.close()
            if (id.length > 0) {
                App.deleteProfile(id)
            }
        }
        contentItem: Label {
            text: "Permanently delete this account and all its messages from this "
                + "device? Make sure you have a backup if you might need it again. "
                + "This cannot be undone."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
}
