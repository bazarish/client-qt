// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

WindowPopup {
    id: root
    bottomMargin: typing ? App.keyboardInset : 0
    property var session: null
    signal showInvite()
    signal showSignWithKey()
    signal showStorage()
    signal showConnectionLog()
    signal showIdentifiers()

    readonly property bool overviewFailed: session !== null && session.overviewFailed
    readonly property string awaitingText: overviewFailed ? qsTr("Unknown") : qsTr("Asking your server...")
    function shownColor(fresh, color) {
        return fresh ? color : Theme.textStale
    }

    onOpened: if (session) { session.watchOverview(true) }
    onClosed: if (session) { session.watchOverview(false) }

    property string pendingExportFile: ""
    property bool pendingExportPrimary: true
    readonly property int kDeviceActionSize: 22
    readonly property int kDevicesActionGap: 10
    readonly property int kDeviceActionIcon: 14
    readonly property real kDeviceGapSpaces: 1.5

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Account")
            onCloseClicked: root.close()
        }

        Rectangle {
            visible: root.session !== null && root.session.deviceRevoked
            Layout.fillWidth: true
            implicitHeight: revokedNote.implicitHeight + 2 * Theme.contentInset
            color: Theme.dangerFill
            Label {
                id: revokedNote
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.contentInset
                anchors.rightMargin: Theme.contentInset
                text: qsTr("Device key revoked")
                color: Theme.dangerInk
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
        }

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                // Not parent.width: the parent sizes to the content, so that binding is circular.
                width: settingsScroll.availableWidth
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Item {
                            id: avatarSlot
                            implicitWidth: 56
                            implicitHeight: 56
                            readonly property bool busy: root.session ? root.session.avatarBusy : false
                            Avatar {
                                anchors.fill: parent
                                fingerprint: root.session ? root.session.fingerprint : ""
                                size: 56
                                opacity: avatarSlot.busy ? 0.35 : 1.0
                            }
                            BusyIndicator {
                                anchors.centerIn: parent
                                running: avatarSlot.busy
                                visible: avatarSlot.busy
                                implicitWidth: 40
                                implicitHeight: 40
                            }
                            TapHandler {
                                enabled: !avatarSlot.busy
                                onTapped: ownAvatar.open()
                            }
                            HoverHandler {
                                enabled: !avatarSlot.busy
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: root.session ? root.session.displayName : ""
                                color: Theme.text
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                TapHandler {
                                    onTapped: {
                                        renameSelfField.text
                                            = root.session ? root.session.displayName : ""
                                        renameSelfDialog.open()
                                    }
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                            Fingerprint {
                                id: fingerprintLine
                                property bool copied: false
                                Layout.fillWidth: true
                                value: root.session ? root.session.fingerprint : ""
                                replacement: copied ? qsTr("Copied to clipboard") : ""
                                color: copied ? Theme.green : Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                Timer {
                                    id: copiedTimer
                                    interval: 1500
                                    onTriggered: fingerprintLine.copied = false
                                }
                                TapHandler {
                                    onTapped: {
                                        if (!root.session) {
                                            return
                                        }
                                        App.copyText(root.session.fingerprint)
                                        fingerprintLine.copied = true
                                        copiedTimer.restart()
                                    }
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }
                        IconButton {
                            iconName: "link"
                            Layout.alignment: Qt.AlignVCenter
                            onClicked: { root.close(); root.showInvite() }
                        }
                    }
                    Label {
                        id: ownStatusLine
                        readonly property string status: root.session ? root.session.ownStatus : ""
                        readonly property int kPreviewLines: 3
                        Layout.fillWidth: true
                        text: status.length > 0 ? status : qsTr("Set a status")
                        color: status.length > 0 ? Theme.text : Theme.textDim
                        wrapMode: Text.Wrap
                        maximumLineCount: kPreviewLines
                        elide: Text.ElideRight
                        TapHandler {
                            onTapped: {
                                statusField.text = ownStatusLine.status
                                statusDialog.open()
                            }
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            iconName: "key"
                            text: qsTr("Sign with the account key")
                            enabled: root.session !== null && root.session.primaryDevice
                            onClicked: { root.close(); root.showSignWithKey() }
                        }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 6
                    Label {
                        text: qsTr("Server connection")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: !(root.session && root.session.connected)
                            ? qsTr("Not configured")
                            : (root.session.reachable
                                ? qsTr("Connected")
                                : qsTr("Not connected"))
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
                            color: (root.session && root.session.reachable) ? Theme.greenFill : Theme.warn
                        }
                        MiddleElided {
                            Layout.fillWidth: true
                            full: root.session && root.session.activeFacadeHost.length > 0
                                ? (root.session.reachable
                                    ? root.session.activeFacadeHost
                                    : qsTr("connecting via %1").arg(root.session.activeFacadeHost))
                                : ""
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                        }
                    }
                    Label {
                        visible: root.session && root.session.configuredFacades.length > 1
                        text: root.session
                            ? qsTr("%1 facades configured (failover)")
                                .arg(root.session.configuredFacades.length)
                            : ""
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ColumnLayout {
                        id: storageSection
                        Layout.fillWidth: true
                        spacing: 3
                        readonly property var info: root.session ? root.session.storageInfo : ({})
                        readonly property bool ok: info.mailboxOk === true
                        readonly property bool fresh: info.fresh === true
                        readonly property bool nearlyFull: root.session !== null
                            && root.session.storageNearlyFull
                        readonly property bool full: root.session !== null && root.session.storageFull
                        readonly property double used: info.mailboxUsed !== undefined
                            ? info.mailboxUsed : 0
                        readonly property double quota: info.mailboxQuota !== undefined
                            ? info.mailboxQuota : 0
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: storageRow.implicitHeight + 10
                            ColumnLayout {
                                id: storageRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: qsTr("Server storage usage")
                                        color: Theme.textDim
                                        font.pixelSize: Theme.fontSmall
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                    }
                                    Label {
                                        text: storageSection.ok
                                            ? (App.humanBytes(storageSection.used) + " / "
                                                + App.humanBytes(storageSection.quota))
                                            : root.awaitingText
                                        color: storageSection.ok && storageSection.nearlyFull
                                            ? Theme.danger
                                            : root.shownColor(storageSection.ok && storageSection.fresh,
                                                Theme.textDim)
                                        font.pixelSize: Theme.fontSmall
                                    }
                                }
                                ProgressBar {
                                    id: storageBar
                                    visible: storageSection.ok
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 5
                                    from: 0
                                    to: 1
                                    value: (storageSection.ok && storageSection.quota > 0)
                                        ? Math.min(1, storageSection.used / storageSection.quota)
                                        : 0
                                    contentItem: Item {
                                        Rectangle {
                                            width: storageBar.visualPosition * parent.width
                                            height: parent.height
                                            radius: height / 2
                                            color: storageSection.nearlyFull ? Theme.danger
                                                : (storageSection.fresh ? Theme.green : Theme.textStale)
                                        }
                                    }
                                }
                                Label {
                                    visible: storageSection.ok && storageSection.nearlyFull
                                    text: storageSection.full
                                        ? qsTr("The server mailbox is full, new messages are not accepted")
                                        : qsTr("The server mailbox is almost full")
                                    color: Theme.danger
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            id: serverConnectionButton
                            Layout.fillWidth: true
                            iconName: "server"
                            text: qsTr("Server connection")
                            onClicked: serverChoice.open()
                        }
                        MenuButton {
                            Layout.preferredWidth: serverConnectionButton.height
                            Layout.preferredHeight: serverConnectionButton.height
                            leftPadding: 0
                            rightPadding: 0
                            iconName: "list"
                            Accessible.name: qsTr("Connection log")
                            onClicked: { root.close(); root.showConnectionLog() }
                        }
                    }
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: "satellite"
                        text: qsTr("Account identifiers")
                        onClicked: { root.close(); root.showIdentifiers() }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 6
                    Label {
                        text: qsTr("Devices")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: root.session ? root.session.ownDevices : []
                        delegate: RowLayout {
                            id: deviceRow
                            required property var modelData
                            readonly property color ink: modelData.revoked ? Theme.danger
                                : (modelData.current ? Theme.text : Theme.textDim)
                            readonly property bool manages: root.session !== null
                                && root.session.primaryDevice && !modelData.current && !modelData.revoked
                            readonly property bool showsQueue: !modelData.revoked && !modelData.current
                                && modelData.queue > 0 && root.session !== null && root.session.overviewKnown
                            readonly property color queueInk: showsQueue
                                ? root.shownColor(root.session.overviewFresh, ink) : ink
                            readonly property real lineInset: Math.max(0, (root.kDeviceActionSize - deviceMetrics.height) / 2)
                            Layout.fillWidth: true
                            Layout.minimumHeight: root.kDeviceActionSize
                            spacing: deviceMetrics.advanceWidth(" ") * root.kDeviceGapSpaces
                            Fingerprint {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: deviceRow.lineInset
                                Layout.preferredWidth: implicitWidth
                                value: deviceRow.modelData.deviceId
                                whole: true
                                color: deviceRow.ink
                                font.pixelSize: Theme.fontSmall
                            }
                            Flow {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: deviceRow.lineInset
                                spacing: deviceMetrics.advanceWidth(" ")
                                FontMetrics { id: deviceMetrics; font.pixelSize: Theme.fontSmall }
                                Label {
                                    text: deviceRow.modelData.primary ? qsTr("Primary") : qsTr("Secondary")
                                    color: deviceRow.ink
                                    font.pixelSize: Theme.fontSmall
                                }
                                Label {
                                    readonly property string status: deviceRow.modelData.revoked ? qsTr("Revoked")
                                        : (deviceRow.modelData.current ? qsTr("(this device)")
                                            : (deviceRow.modelData.departed ? qsTr("(removed from that device)")
                                                : (!deviceRow.modelData.registered ? qsTr("(inactive)") : "")))
                                    visible: status.length > 0
                                    text: status
                                    color: deviceRow.ink
                                    font.pixelSize: Theme.fontSmall
                                }
                                Row {
                                    visible: deviceRow.showsQueue
                                    Label {
                                        text: "(" + qsTr("queue %1").arg(deviceRow.modelData.queue)
                                            + (deviceRow.manages ? " " : ")")
                                        color: deviceRow.queueInk
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    Icon {
                                        visible: deviceRow.manages
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "close"
                                        size: Theme.fontSmall
                                        color: clearArea.containsMouse ? Theme.danger : Theme.green
                                        MouseArea {
                                            id: clearArea
                                            anchors.fill: parent
                                            anchors.margins: -root.kDeviceActionIcon / 2
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                clearConfirm.deviceId = deviceRow.modelData.deviceId
                                                clearConfirm.open()
                                            }
                                        }
                                    }
                                    Label {
                                        visible: deviceRow.manages
                                        text: ")"
                                        color: deviceRow.queueInk
                                        font.pixelSize: Theme.fontSmall
                                    }
                                }
                            }
                            IconButton {
                                visible: deviceRow.manages
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: root.kDeviceActionSize
                                implicitHeight: root.kDeviceActionSize
                                iconName: "close"
                                iconSize: root.kDeviceActionIcon
                                tint: Theme.danger
                                Accessible.name: qsTr("Revoke")
                                onClicked: {
                                    revokeConfirm.deviceId = deviceRow.modelData.deviceId
                                    revokeConfirm.open()
                                }
                            }
                        }
                    }
                    }
                    MenuButton {
                        visible: root.session !== null && root.session.ownDevices.some(
                            (device) => !device.current && !device.revoked)
                        Layout.fillWidth: true
                        Layout.topMargin: root.kDevicesActionGap
                        iconName: "devices"
                        text: qsTr("Ask my other devices for contacts")
                        onClicked: root.session.askForContacts()
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 8
                    Label { text: qsTr("Privacy"); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Allow incoming calls")
                                color: Theme.text
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("Off, a caller is refused automatically")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            checked: root.session ? root.session.acceptCalls : true
                            onToggled: if (root.session) root.session.acceptCalls = checked
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Send read receipts")
                                color: Theme.text
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label { text: qsTr("Lets contacts see a green mark when you receive"); color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        Toggle {
                            checked: root.session ? root.session.sendReceipts : true
                            onToggled: if (root.session) root.session.sendReceipts = checked
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Show online status")
                                color: Theme.text
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label { text: qsTr("Contacts see when the app is open and when you type, and you see theirs"); color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        Toggle {
                            checked: root.session ? root.session.showPresence : true
                            onToggled: if (root.session) root.session.showPresence = checked
                        }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 8
                    Label { text: qsTr("Database"); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: "lock"
                        text: qsTr("Change password")
                        onClicked: {
                            newPass.text = ""
                            newPassAgain.text = ""
                            passwordDialog.open()
                        }
                    }
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: "save"
                        text: qsTr("Back up this account")
                        enabled: root.session !== null && root.session.primaryDevice
                        onClicked: exportRole.open()
                    }
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: "devices"
                        text: qsTr("Connect a new device online")
                        enabled: root.session !== null && root.session.primaryDevice
                        onClicked: pairRole.open()
                    }
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: "disk"
                        text: qsTr("Disk usage")
                        onClicked: { root.close(); root.showStorage() }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            visible: App.activeLockable
                            iconName: "lock"
                            text: qsTr("Lock account")
                            onClicked: {
                                const id = root.session.accountId
                                root.close()
                                App.lockAccount(id)
                            }
                        }
                        MenuButton {
                            Layout.fillHeight: true
                            Layout.fillWidth: !App.activeLockable
                            enabled: App.deletingId.length === 0 && root.session !== null
                            iconName: "trash"
                            text: App.activeLockable ? ""
                                : App.deletingId.length > 0 ? qsTr("Deleting...") : qsTr("Delete account")
                            Accessible.name: qsTr("Delete account")
                            danger: true
                            onClicked: {
                                deleteDialog.localOnly = !root.session.primaryDevice
                                deleteDialog.show(root.session.accountId, root.session.displayName,
                                    root.session.primaryDevice, root.session.ownDevices.some(
                                        (device) => device.primary && !device.current && !device.revoked))
                            }
                        }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 8
                    Label { text: qsTr("Block list"); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    readonly property var rows: (root.session && root.session.contactsRevision >= 0)
                        ? root.session.blockedList() : []
                    Label {
                        visible: parent.rows.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("Nobody. Blocking is offered in a contact's panel.")
                    }
                    Repeater {
                        model: parent.rows
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            MiddleElided {
                                Layout.fillWidth: true
                                full: modelData.name
                                color: Theme.text
                            }
                            MenuButton {
                                iconName: "check"
                                text: qsTr("Unblock")
                                enabled: root.session !== null && root.session.online
                                onClicked: root.session.setBlocked(modelData.fingerprint, false)
                            }
                        }
                    }
                }
            }
        }
    }

    property string backupName: (root.session && root.session.displayName.length > 0
        ? root.session.displayName.replace(/[^A-Za-z0-9._-]+/g, "_") : "bazarish")
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "bazarish"
        nameFilters: ["Bazarish backup (*.bazarish)", "All files (*)"]
        // A bare "file:///name" opens the chooser at the filesystem root.
        currentFile: root.session
            ? root.session.defaultSaveUrl(root.backupName + ".bazarish") : ""
        onAccepted: { root.pendingExportFile = selectedFile; exportPassDialog.open() }
    }
    AvatarViewer {
        id: ownAvatar
        fingerprint: root.session ? root.session.fingerprint : ""
        own: true
        removable: root.session ? root.session.hasAvatar : false
        onChangeRequested: App.mobile && App.canPickPictures ? App.pickAvatar() : avatarDialog.open()
        onRemoveRequested: dropAvatarDialog.open()
    }

    FileDialog {
        id: avatarDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: if (root.session) { avatarCrop.openFor(selectedFile) }
    }

    Connections {
        target: App
        function onAvatarPickFailed(reason) { window.showToast(reason) }
        function onAvatarPicked(uri) {
            if (uri.length > 0) {
                avatarCrop.openFor(uri)
            }
        }
    }

    AvatarCropDialog {
        id: avatarCrop
        session: root.session
        onCropped: function(grab) { if (root.session) { root.session.setAvatarFromGrab(grab) } }
    }

    Dialog {
        id: clearConfirm
        property string deviceId: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, root.width - Theme.windowSideRoom)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Clear this device's queue?")
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Clear")
            danger: true
            onAccepted: clearConfirm.accept()
            onRejected: clearConfirm.reject()
        }
        onAccepted: if (root.session && clearConfirm.deviceId.length > 0) {
            root.session.clearDeviceQueue(clearConfirm.deviceId)
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: qsTr("The server deletes the mail for %1. The device registers again the next time it connects.")
                    .arg((clearConfirm.deviceId.match(/.{1,4}/g) || []).join(" "))
            }
        }
    }

    Dialog {
        id: revokeConfirm
        property string deviceId: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, root.width - Theme.windowSideRoom)
        background: DialogFrame { destructive: true }
        header: WindowHeader {
            title: qsTr("Revoke this device?")
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Revoke")
            danger: true
            onAccepted: revokeConfirm.accept()
            onRejected: revokeConfirm.reject()
        }
        onAccepted: if (root.session && revokeConfirm.deviceId.length > 0) {
            root.session.revokeDevice(revokeConfirm.deviceId)
        }
        contentItem: ColumnLayout {
            spacing: 8
            Fingerprint {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                value: revokeConfirm.deviceId
                whole: true
                color: Theme.text
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: qsTr("The device will lose access to the account and its new mail. This cannot be undone.")
            }
        }
    }

    DeviceRoleDialog {
        id: exportRole
        onChosen: (primary) => { root.pendingExportPrimary = primary; exportDialog.open() }
    }
    DeviceRoleDialog {
        id: pairRole
        onChosen: (primary) => { pairDeviceSheet.primary = primary; pairDeviceSheet.open() }
    }

    Dialog {
        id: dropAvatarDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, root.width - Theme.windowSideRoom)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Remove your avatar?")
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Remove")
            danger: true
            onAccepted: dropAvatarDialog.accept()
            onRejected: dropAvatarDialog.reject()
        }
        onAccepted: if (root.session) { root.session.clearAvatar() }
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.textDim
            padding: 14
            text: qsTr("You and your contacts see the generated picture again")
        }
    }
    Dialog {
        id: renameSelfDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        // Without a width of its own the dialog grows to fit its text on one line.
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        title: qsTr("Change name")
        onAccepted: if (root.session) root.session.setDisplayName(renameSelfField.text)
        background: DialogFrame { }
        header: WindowHeader {
            title: renameSelfDialog.title
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Save")
            acceptEnabled: renameSelfField.displayText.trim().length > 0
            onAccepted: renameSelfDialog.accept()
            onRejected: renameSelfDialog.reject()
        }
        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: renameSelfField
                Layout.fillWidth: true
                placeholderText: qsTr("Your name")
                validator: Utf8Limit { maxBytes: App.maxAccountNameBytes }
                color: Theme.text
                placeholderTextColor: Theme.textDim
                onAccepted: renameSelfDialog.accept()
            }
            Label {
                text: qsTr("Only updates your invite link")
                color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }
    }

    Dialog {
        id: statusDialog
        readonly property string entered: statusField.text.trim()
        readonly property bool fits: root.session !== null && root.session.statusFits(statusField.text)
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        title: qsTr("Your status")
        onAccepted: if (root.session) root.session.setStatus(statusField.text)
        background: DialogFrame { }
        header: WindowHeader {
            title: statusDialog.title
            showClose: false
        }
        footer: DialogButtons {
            acceptText: qsTr("Save")
            acceptEnabled: statusDialog.fits && root.session !== null
                && statusDialog.entered !== root.session.ownStatus
            onAccepted: statusDialog.accept()
            onRejected: statusDialog.reject()
        }
        contentItem: ColumnLayout {
            spacing: 8
            TextArea {
                id: statusField
                Layout.fillWidth: true
                placeholderText: qsTr("Seen by your contacts")
                wrapMode: TextEdit.Wrap
                color: Theme.text
                placeholderTextColor: Theme.textDim
                background: Rectangle {
                    radius: 8
                    color: Theme.control
                    border.color: statusField.activeFocus ? Theme.accent : Theme.controlEdge
                }
            }
            Label {
                Layout.alignment: Qt.AlignRight
                text: Array.from(statusDialog.entered).length + "/" + App.maxStatusChars
                color: statusDialog.fits ? Theme.textDim : Theme.danger
                font.pixelSize: Theme.fontSmall
            }
        }
    }

    Dialog {
        id: exportPassDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        focus: true
        title: qsTr("Backup password")
        onAccepted: root.session.exportAccount(
            root.pendingExportFile, exportPass.text, root.pendingExportPrimary)
        background: DialogFrame { }
        header: WindowHeader {
            title: exportPassDialog.title
            showClose: false
        }
        footer: DialogButtons { onAccepted: exportPassDialog.accept(); onRejected: exportPassDialog.reject() }
        contentItem: TextField { id: exportPass; echoMode: TextInput.Password; placeholderText: qsTr("password"); color: Theme.text; placeholderTextColor: Theme.textDim; implicitWidth: 260; onAccepted: exportPassDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: exportPass.activeFocus ? Theme.accent : Theme.border } }
    }

    Dialog {
        id: passwordDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
        focus: true
        title: qsTr("Database password")
        background: DialogFrame { }
        header: WindowHeader {
            title: passwordDialog.title
            showClose: false
        }
        readonly property bool matched: newPass.text === newPassAgain.text
        readonly property bool reserved: newPass.text === App.reservedPassphrase
        readonly property bool choosable: matched && !reserved
        onAccepted: root.session.changePassphrase(newPass.text)
        footer: DialogButtons {
            acceptEnabled: passwordDialog.choosable
            onAccepted: passwordDialog.accept()
            onRejected: passwordDialog.reject()
        }
        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: newPass
                echoMode: TextInput.Password
                placeholderText: qsTr("new password")
                color: Theme.text; placeholderTextColor: Theme.textDim
                Layout.fillWidth: true; implicitWidth: 260
            }
            TextField {
                id: newPassAgain
                echoMode: TextInput.Password
                placeholderText: qsTr("repeat it")
                color: Theme.text; placeholderTextColor: Theme.textDim
                Layout.fillWidth: true; implicitWidth: 260
                onAccepted: if (passwordDialog.choosable) { passwordDialog.accept() }
            }
            Label {
                text: passwordDialog.reserved
                    ? qsTr("This passphrase cannot be used")
                    : passwordDialog.matched
                    ? (newPass.displayText.length === 0
                        ? qsTr("Empty: this account will be kept unencrypted")
                        : qsTr("Asked for whenever this account is opened"))
                    : qsTr("The two do not match")
                color: passwordDialog.choosable ? Theme.textDim : Theme.warn
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }
    }

    Dialog {
        id: connectionDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        parent: Overlay.overlay
        width: Math.min(Theme.windowWidth, parent ? parent.width - Theme.windowSideRoom : Theme.windowWidth)
        height: Math.min(parent ? parent.height - Theme.windowTopRoom : implicitHeight, implicitHeight)
        margins: Theme.spacing
        bottomMargin: Theme.spacing + (activeFocus ? App.keyboardInset : 0)
        onOpened: connectionForm.reset()
        padding: 0
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Server connection")
            showBack: true
            onBackClicked: { connectionDialog.close(); serverChoice.open() }
            onCloseClicked: connectionDialog.close()
        }
        contentItem: ScrollColumn {
            inset: Theme.contentInset
            spacing: 12
            ServerConnectForm {
                id: connectionForm
                Layout.fillWidth: true
                session: root.session
                actionText: (root.session && !root.session.online) ? qsTr("Save") : qsTr("Connect")
                initialFacades: root.session ? root.session.configuredFacades : []
                initialFingerprint: root.session ? root.session.serverFingerprint : ""
                initialReseeds: root.session ? root.session.configuredReseeds : []
                onSubmitted: connectionDialog.close()
            }
        }
    }

    Dialog {
        id: serverChoice
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, (Overlay.overlay ? Overlay.overlay.width : Theme.dialogWidth) - Theme.windowSideRoom)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Server connection")
            onCloseClicked: serverChoice.close()
        }
        contentItem: ButtonRow {
            MenuButton {
                Layout.fillWidth: true
                iconName: "gear"
                text: qsTr("Configure")
                onClicked: { serverChoice.close(); connectionDialog.open() }
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "qr"
                text: qsTr("Share")
                enabled: root.session && root.session.serverLink.length > 0
                onClicked: { serverChoice.close(); serverShare.openFrom(serverChoice) }
            }
        }
    }
    ShareLinkSheet {
        id: serverShare
        title: qsTr("Share server")
        note: qsTr("This link connects another account to your server")
        link: root.session ? root.session.serverLink : ""
    }

    AccountDeleteConfirmDialog {
        id: deleteDialog
        onLocalOnlyRequested: (id) => {
            root.close()
            App.leaveAccount(id)
        }
        onConfirmed: (id) => {
            root.close()
            if (id.length > 0) {
                App.deleteAccount(id)
            }
        }
    }

    PairDeviceSheet {
        id: pairDeviceSheet
        session: root.session
    }
}
