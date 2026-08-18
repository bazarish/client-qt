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

    // Refresh the per-user I2P destination status and storage usage on open.
    onOpened: if (session) { session.refreshI2pStatus(); session.refreshStorageUsage() }

    // Ticks every few seconds while Settings is open so the storage "updated N ago"
    // age stays current without the user reopening the page.
    property int agoTick: 0
    Timer { running: root.visible; interval: 5000; repeat: true; onTriggered: root.agoTick++ }

    // Human-readable byte count (B / KB / MB / GB).
    function humanBytes(n) {
        if (!n || n <= 0) {
            return "0 B"
        }
        const u = ["B", "KB", "MB", "GB", "TB"]
        var v = n
        var i = 0
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return (i === 0 ? v : v.toFixed(1)) + " " + u[i]
    }

    // "updated N ago" from a wall-clock-ms timestamp (0 = never). Reads agoTick so it
    // re-evaluates as the timer ticks.
    function agoText(updatedAtMs) {
        void root.agoTick
        if (!updatedAtMs || updatedAtMs <= 0) {
            return "never updated"
        }
        var s = Math.max(0, Math.floor((Date.now() - updatedAtMs) / 1000))
        if (s < 5) {
            return "updated just now"
        }
        if (s < 60) {
            return "updated " + s + "s ago"
        }
        if (s < 3600) {
            return "updated " + Math.floor(s / 60) + "m ago"
        }
        if (s < 86400) {
            return "updated " + Math.floor(s / 3600) + "h ago"
        }
        return "updated " + Math.floor(s / 86400) + "d ago"
    }

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
                        // Tap the avatar to view it full-size; set a new photo with
                        // the button below.
                        Avatar {
                            fingerprint: root.session ? root.session.fingerprint : ""
                            size: 56
                            enlargeable: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: root.session ? root.session.displayName : ""; color: Theme.text; font.weight: Font.Medium }
                            Label { text: root.session ? root.session.shortFingerprint(root.session.fingerprint) : ""; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    MenuButton { Layout.fillWidth: true; text: "Set photo…"; onClicked: avatarDialog.open() }
                    MenuButton {
                        Layout.fillWidth: true
                        text: "Change name…"
                        onClicked: {
                            renameSelfField.text = root.session ? root.session.displayName : ""
                            renameSelfDialog.open()
                        }
                    }
                    MenuButton { Layout.fillWidth: true; text: "Show my invite / QR"; onClicked: { root.close(); root.showInvite() } }
                    MenuButton { Layout.fillWidth: true; text: "Sign in with key (portals / sites)"; onClicked: { root.close(); root.showSignWithKey() } }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // App & privacy — GLOBAL settings, shared by every profile on this
                // device (the per-profile sections are below).
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "App & privacy (all profiles)"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Full privacy mode"; color: Theme.text }
                            Label {
                                text: "Refuse every clearnet connection — reach servers over I2P only. A profile with no I2P facade goes offline."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Switch {
                            checked: App.fullPrivacyMode
                            onToggled: App.setFullPrivacyMode(checked)
                        }
                    }
                    // Sticky I2P: once this profile has reached its server over I2P it
                    // refuses clearnet, so a flaky link cannot move it back silently.
                    // This is the deliberate way back.
                    RowLayout {
                        visible: root.session && root.session.hasI2pFacade
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Allow clearnet for this profile"; color: Theme.text }
                            Label {
                                text: root.session && root.session.clearnetAllowed
                                    ? "This profile may fall back to a clearnet facade — your server then sees this device's address."
                                    : "This profile reaches its server over I2P and refuses clearnet. Turn on only if you accept being seen by address."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Switch {
                            checked: root.session && root.session.clearnetAllowed
                            onToggled: root.session.allowClearnet(checked)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "I2P engine (libi2pd)"; color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                        Label { text: App.i2pdVersion; color: Theme.text; font.pixelSize: Theme.fontSmall }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Connection
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 6
                    Label { text: "Server connection"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    // Under full privacy mode a profile with no I2P facade cannot
                    // reach its server at all (clearnet is refused), so its status is
                    // an explicit I2P-only offline error rather than a vague "connecting".
                    readonly property bool i2pOnlyBlocked: App.fullPrivacyMode
                        && root.session && root.session.connected && !root.session.hasI2pFacade
                    Label {
                        text: parent.i2pOnlyBlocked
                            ? "Offline — full privacy mode is on, but this profile has no I2P facade. Add one (or turn privacy mode off) to connect."
                            : (!(root.session && root.session.connected)
                                ? "No server configured"
                                : (root.session.reachable
                                    ? "Connected"
                                    : "Not reaching the server — Connect to finish setup"))
                        color: parent.i2pOnlyBlocked
                            ? Theme.danger
                            : ((root.session && root.session.connected && !root.session.reachable)
                                ? Theme.warn : Theme.text)
                        font.weight: parent.i2pOnlyBlocked ? Font.DemiBold : Font.Normal
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

                // Storage — this profile's usage on its two backends (server-core
                // with how long ago the figures were taken
                // so an offline profile still shows its last-known usage.
                ColumnLayout {
                    id: storageSection
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    readonly property var info: root.session ? root.session.storageInfo : ({})
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Storage"; color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                        Label {
                            text: storageSection.info ? root.agoText(storageSection.info.updatedAt) : ""
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                        }
                    }

                    // One backend's used / free / total with a usage bar. `used`,
                    // `quota` and `ok` come from storageInfo; quota is the total.
                    component StorageRow: ColumnLayout {
                        property string title: ""
                        property double used: 0
                        property double quota: 0
                        property bool ok: false
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: title; color: Theme.text; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                            Label {
                                text: ok
                                    ? (root.humanBytes(used) + " / " + root.humanBytes(quota))
                                    : "unavailable"
                                color: ok ? Theme.textDim : Theme.warn
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 5
                            from: 0; to: 1
                            value: (ok && quota > 0) ? Math.min(1, used / quota) : 0
                        }
                        Label {
                            visible: ok
                            text: root.humanBytes(Math.max(0, quota - used)) + " free"
                            color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                        }
                    }

                    // Flashed on refresh: figures that come back unchanged are the
                    // common case, so without it the button reads as a no-op.
                    Rectangle {
                        id: storageBox
                        Layout.fillWidth: true
                        radius: Theme.radiusSmall
                        color: "transparent"
                        implicitHeight: mailboxRow.implicitHeight + 12
                        SequentialAnimation {
                            id: storageFlash
                            PropertyAction { target: storageBox; property: "color"; value: Qt.rgba(0.12, 0.48, 0.08, 0.5) }
                            PauseAnimation { duration: 550 }
                            ColorAnimation { target: storageBox; property: "color"; to: "transparent"; duration: 500 }
                        }
                        StorageRow {
                            id: mailboxRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            title: "Mailbox"
                            used: storageSection.info ? storageSection.info.mailboxUsed : 0
                            quota: storageSection.info ? storageSection.info.mailboxQuota : 0
                            ok: storageSection.info ? storageSection.info.mailboxOk : false
                        }
                    }
                    MenuButton {
                        Layout.alignment: Qt.AlignRight
                        text: "Refresh"
                        onClicked: { if (root.session) root.session.refreshStorageUsage(); storageFlash.restart() }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Personal I2P destination
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Your I2P destination"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label {
                        text: "Your account is reached at a destination of its own — the key is yours, so the address stays the same if you move to another server. Your server only ever holds a short delegation, re-issued in the background; publishing hands it your card so contacts can route to you."
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
                                text: root.session && root.session.i2pActive && root.session.i2pTransientExpires > 0
                                    ? ("Delegated until " + Qt.formatDate(new Date(root.session.i2pTransientExpires * 1000), "yyyy-MM-dd"))
                                    : "Not published"
                                color: (root.session && root.session.i2pActive && root.session.i2pTransientExpires > 0)
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
                            text: "Publish"
                            enabled: root.session && root.session.connected
                            onClicked: root.session.publishPersonalDest()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && root.session.i2pEnabled
                            text: "Revoke"
                            onClicked: root.session.disablePersonalDest()
                        }
                        // Re-poll the server status and flash the box for ~1s.
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Refresh"
                            onClicked: { if (root.session) root.session.refreshI2pStatus(); i2pFlash.restart() }
                        }
                    }
                    // Permanently drop this profile's master key: nobody can reach it again
                    // until a fresh destination is published.
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
    // Change the account's own display name. Local only: it updates this device and
    // the name carried in future invite descriptors; existing contacts keep the
    // local name they have for us (it is never sent to them).
    Dialog {
        id: renameSelfDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Change name"
        onAccepted: if (root.session) root.session.setDisplayName(renameSelfField.text)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Change name"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { acceptText: "Save"; onAccepted: renameSelfDialog.accept(); onRejected: renameSelfDialog.reject() }
        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: renameSelfField
                Layout.fillWidth: true
                implicitWidth: 300
                placeholderText: "Your name"
                color: Theme.text
                placeholderTextColor: Theme.textDim
                onAccepted: renameSelfDialog.accept()
                background: Rectangle { radius: 8; color: Theme.surface; border.color: renameSelfField.activeFocus ? Theme.accent : Theme.border }
            }
            Label {
                text: "Only updates this device and your invite link. Your contacts keep the name they gave you."
                color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }
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
                + "Nobody can reach you until you publish a new destination, and that "
                + "one would be a new, different address."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
}
