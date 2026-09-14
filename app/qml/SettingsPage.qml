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
    signal showConnectionLog()

    modal: true
    anchors.centerIn: Overlay.overlay
    // Fits the window it is shown in: on a narrow screen the panel used to keep
    // its 460 and hang off both edges.
    width: Math.min(460, parent ? parent.width - 24 : 460)
    height: Math.min(parent ? parent.height - 40 : 600, 640)
    padding: 0

    // Refresh the per-user I2P destination status and storage usage on open.
    onOpened: if (session) { session.refreshI2pStatus(); session.refreshStorageUsage();
        session.refreshDevices(); delegationDaysBox.value = session.delegationDays }

    // Ticks every few seconds while Settings is open so the storage "updated N ago"
    // age stays current without the user reopening the page.
    property int agoTick: 0
    Timer { running: root.visible; interval: 5000; repeat: true; onTriggered: root.agoTick++ }

    // And asks the server again while the page is open: the quota, the devices
    // collecting mail and whether this account's address is published are all
    // things that change while somebody is watching them.
    Timer {
        running: root.visible && root.session
        interval: 10000
        repeat: true
        onTriggered: {
            root.session.refreshI2pStatus()
            root.session.refreshStorageUsage()
            root.session.refreshDevices()
        }
    }

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
            Label { text: "Account"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                // Bound to what the scroll view actually offers. Taking the
                // popup's width instead left the column wider than the visible
                // area, and everything on its right - toggles, values, buttons -
                // was cut off at the panel edge; taking `parent.width` is
                // circular here, because the parent sizes to the content.
                width: settingsScroll.availableWidth
                spacing: 14

                // Account
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        // The picture and the name are their own controls: tap the
                        // picture to choose a new one, the name to rename. Two
                        // buttons repeating what the things themselves can do were
                        // just the same actions written twice.
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
                            // Compressing the picture and handing it to every
                            // contact runs over I2P: the wait belongs on the
                            // picture that is changing, not only in the activity
                            // panel behind this window.
                            BusyIndicator {
                                anchors.centerIn: parent
                                running: avatarSlot.busy
                                visible: avatarSlot.busy
                                implicitWidth: 40
                                implicitHeight: 40
                            }
                            TapHandler {
                                enabled: !avatarSlot.busy
                                onTapped: avatarDialog.open()
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
                            // Tap the fingerprint to copy the whole thing; the line
                            // says what happened for a moment, so the click is not
                            // a guess.
                            Label {
                                id: fingerprintLine
                                property bool copied: false
                                text: copied
                                    ? "Copied to clipboard"
                                    : (root.session
                                        ? root.session.shortFingerprint(root.session.fingerprint)
                                        : "")
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
                                        root.session.copyText(root.session.fingerprint)
                                        fingerprintLine.copied = true
                                        copiedTimer.restart()
                                    }
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }
                        // Sharing yourself lives at the far edge of the same row.
                        IconButton {
                            iconName: "link"
                            Layout.alignment: Qt.AlignVCenter
                            onClicked: { root.close(); root.showInvite() }
                        }
                    }
                    MenuButton { Layout.fillWidth: true; text: "Sign in with your key…"; onClicked: { root.close(); root.showSignWithKey() } }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // App & privacy — GLOBAL settings, shared by every account on this
                // device (the per-account sections are below).
                // Connection
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 6
                    Label { text: "Server connection"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label {
                        text: !(root.session && root.session.connected)
                            ? "Not configured"
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
                            text: root.session && root.session.activeFacadeHost.length > 0
                                ? ((root.session.reachable ? "via " : "connecting via ") + root.session.activeFacadeHost)
                                : ""
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle
                        }
                    }
                    Label {
                        visible: root.session && root.session.configuredFacades.length > 1
                        text: root.session ? (root.session.configuredFacades.length + " facades configured (failover)") : ""
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }
                    // The account's aliases in the central registry. Nothing is
                    // asked of the registry until this button is pressed; after
                    // that the client keeps the aliases their owner asked to
                    // point here pointing here, and leaves the rest alone.
                    //
                    // "Your alias" and not "Your name": the display name below is
                    // a different thing, and both used to be called the same.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        spacing: 3
                        Label { text: "Your alias"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        Label {
                            Layout.fillWidth: true
                            text: root.session && root.session.aliasSummary.length > 0
                                ? root.session.aliasSummary
                                : "Not activated on this device."
                            color: Theme.text; wrapMode: Text.Wrap
                        }
                        Button {
                            text: root.session && root.session.aliasBusy
                                ? "Asking the alias registry..."
                                : "Check my aliases"
                            enabled: root.session && root.session.connected && !root.session.aliasBusy
                            onClicked: root.session.activateAliasServicing()
                        }
                    }
                    // What this account is holding on its server, as one line and a
                    // bar. Tapping it re-polls the server and tints the row, so the
                    // figures are refreshed where they are read.
                    ColumnLayout {
                        id: storageSection
                        Layout.fillWidth: true
                        spacing: 3
                        // An account that has just been closed leaves this with
                        // nothing to read, so every field says so itself rather
                        // than handing undefined to a typed property.
                        readonly property var info: root.session ? root.session.storageInfo : ({})
                        readonly property bool ok: info.mailboxOk === true
                        readonly property double used: info.mailboxUsed !== undefined
                            ? info.mailboxUsed : 0
                        readonly property double quota: info.mailboxQuota !== undefined
                            ? info.mailboxQuota : 0
                        Rectangle {
                            Layout.fillWidth: true
                            radius: Theme.radiusSmall
                            color: "transparent"
                            implicitHeight: storageRow.implicitHeight + 10
                            SequentialAnimation {
                                id: storageFlash
                                PropertyAction { target: parent; property: "color"
                                    value: Qt.rgba(0.12, 0.48, 0.08, 0.5) }
                                PauseAnimation { duration: 550 }
                                ColorAnimation { target: parent; property: "color"
                                    to: "transparent"; duration: 500 }
                            }
                            ColumnLayout {
                                id: storageRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "Server storage usage"
                                        color: Theme.textDim
                                        font.pixelSize: Theme.fontSmall
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: storageSection.ok
                                            ? (root.humanBytes(storageSection.used) + " / "
                                                + root.humanBytes(storageSection.quota))
                                            : "unavailable"
                                        color: storageSection.ok ? Theme.textDim : Theme.warn
                                        font.pixelSize: Theme.fontSmall
                                    }
                                }
                                ProgressBar {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 5
                                    from: 0
                                    to: 1
                                    value: (storageSection.ok && storageSection.quota > 0)
                                        ? Math.min(1, storageSection.used / storageSection.quota)
                                        : 0
                                }
                            }
                            TapHandler {
                                onTapped: {
                                    if (root.session) {
                                        root.session.refreshStorageUsage()
                                    }
                                    storageFlash.restart()
                                }
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                    }
                    // The devices registered on this account. Mail is deleted only
                    // once every one of them has acked it, so a device that is gone
                    // for good holds the mailbox until retention runs out - which is
                    // why its owner needs to be able to drop it.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.session && root.session.connected

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "Devices on this account"
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                Layout.fillWidth: true
                            }
                            Label {
                                text: root.session ? root.session.devices.length : 0
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                        }

                        Repeater {
                            model: root.session ? root.session.devices : []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Label {
                                    // The queue names the device that is holding
                                    // mail without having to know whose id this is.
                                    text: modelData.clientId
                                        + (modelData.current ? "  (this device)" : "")
                                        + (modelData.queue > 0
                                            ? " (queue: " + modelData.queue + ")" : "")
                                    color: modelData.current ? Theme.text : Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                // A link, not a button: one device is one line,
                                // and a plate here would make the row taller than
                                // the thing it acts on.
                                Label {
                                    text: "Forget"
                                    visible: !modelData.current
                                    color: forgetArea.containsMouse ? Theme.danger : Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                    font.underline: forgetArea.containsMouse
                                    MouseArea {
                                        id: forgetArea
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            forgetConfirm.clientId = modelData.clientId
                                            forgetConfirm.open()
                                        }
                                    }
                                }
                            }
                        }

                        Label {
                            visible: root.session && root.session.devices.length === 0
                            text: "Nothing yet - open this while connected to ask your server."
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
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
                        // What the account actually did on the wire: the one
                        // place that shows a refusal, a delivery nobody signed
                        // for, or a device sync that never left.
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Connection log"
                            onClicked: { root.close(); root.showConnectionLog() }
                        }
                    }
                    // A device asks for the address book once, on its first sync.
                    // If no other device was online to answer, this asks again.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Ask my other devices for contacts"
                            enabled: root.session !== null
                            onClicked: root.session.askForContacts()
                        }
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
                    // term line reads "Inactive" whenever the delegation is not
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
                            // Two facts, not one verdict: what the server holds (the
                            // delegation, with its term) and whether the destination
                            // is actually up. "Not published" said neither.
                            Label {
                                readonly property bool delegated: root.session
                                    && root.session.i2pTransientExpires > 0
                                readonly property string serverState: root.session
                                    ? root.session.i2pServerState : ""
                                text: (delegated
                                        ? "Delegated until " + Qt.formatDate(
                                            new Date(root.session.i2pTransientExpires * 1000),
                                            "yyyy-MM-dd")
                                        : "No delegation handed to your server yet")
                                    + " · " + (serverState === "active"
                                        ? "destination up"
                                        : (serverState === "building"
                                            ? "destination coming up (minutes)"
                                            : (serverState === "expired"
                                                ? "delegation expired"
                                                : (serverState.length > 0
                                                    ? "no destination on the server"
                                                    : "server not asked yet"))))
                                color: serverState === "active" ? Theme.success : Theme.warn
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            // The address is the answer to "do I even have a key",
                            // so it is shown whenever there is one - published or not.
                            Label {
                                visible: root.session && root.session.i2pAddress.length > 0
                                text: root.session ? root.session.i2pAddress : ""
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                            Label {
                                visible: root.session && !root.session.i2pHasKey
                                text: "No destination key on this account yet."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            // The server's answer, when it is not the address above.
                            // Mail sent to the address this device shows is not
                            // arriving anywhere, and nothing else on this page
                            // would say so.
                            ColumnLayout {
                                visible: root.session && root.session.i2pAddressMismatch
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: "Your server serves another address"
                                    color: Theme.warn
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: root.session ? root.session.i2pServedAddress : ""
                                    color: Theme.warn
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: "Another device of yours published it. Contacts "
                                        + "write to whichever they know, so until the two "
                                        + "agree some mail reaches nobody."
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                MenuButton {
                                    text: "Publish this device's address"
                                    enabled: root.session && root.session.connected
                                    onClicked: root.session.keepThisDeviceAddress()
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                    // Set up a master key first (generate or load a .dat), then turn it on.
                    RowLayout {
                        visible: root.session && !root.session.i2pHasKey
                        Layout.fillWidth: true; spacing: 8
                        MenuButton { Layout.fillWidth: true; text: "Create address"
                            onClicked: { root.session.generatePersonalKey(); i2pFlash.restart() } }
                        MenuButton { Layout.fillWidth: true; text: "Load an existing key…"; onClicked: i2pKeyDialog.open() }
                    }
                    RowLayout {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true; spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && !root.session.i2pEnabled
                            // Not the normal path: connecting publishes on its own and
                            // the delegation is re-issued in the background. This is for
                            // a key loaded from elsewhere, a revoked destination, or a
                            // publish that did not finish.
                            text: root.session && root.session.i2pBusy ? "Publishing…"
                                : "Publish address"
                            enabled: root.session && root.session.connected
                                && !root.session.i2pBusy
                            onClicked: root.session.publishPersonalDest()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && root.session.i2pEnabled
                            text: root.session && root.session.i2pBusy ? "Taking offline…"
                                : "Take offline"
                            enabled: root.session && !root.session.i2pBusy
                            onClicked: root.session.disablePersonalDest()
                        }
                        // Re-poll the server status and flash the box for ~1s.
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Refresh"
                            onClicked: { if (root.session) root.session.refreshI2pStatus(); i2pFlash.restart() }
                        }
                    }
                    // Permanently drop this account's master key: nobody can reach it again
                    // until a fresh destination is published.
                    MenuButton {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true
                        text: "Delete address"
                        danger: true
                        onClicked: deleteKeyDialog.open()
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Backup
                // What this account alone does. The switches above are the app's;
                // these follow the account wherever it is opened.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Privacy"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    // How long this account hands its address to the server for.
                    // It is the only thing that ties an account to a server in
                    // time, so the user - not the operator - sets it.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Delegation term"; color: Theme.text }
                            Label {
                                text: "Your server can only carry your address while you keep "
                                    + "delegating it, and your client renews at half the term. "
                                    + "Shorter means moving to another server takes effect "
                                    + "sooner; longer means you stay reachable while you are away."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        ColumnLayout {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.rightMargin: 2
                            spacing: 6
                            // One width for both rows: an editable SpinBox asks
                            // for far more room than it needs, and two stacked
                            // controls of different widths read as two unrelated
                            // things. Sized to hold the button's label.
                            readonly property int controlWidth: 140
                            RowLayout {
                                Layout.preferredWidth: parent.controlWidth
                                spacing: 6
                                SpinBox {
                                    id: delegationDaysBox
                                    Layout.fillWidth: true
                                    from: root.session ? root.session.minDelegationDays : 1
                                    to: root.session ? root.session.maxDelegationDays : 30
                                    value: root.session ? root.session.delegationDays : 14
                                    editable: true
                                    // The stock control is a white box the height
                                    // of three lines: on this palette it reads as
                                    // the brightest thing on the page.
                                    readonly property int fieldHeight: 30
                                    implicitHeight: fieldHeight
                                    topPadding: 0
                                    bottomPadding: 0
                                    background: Rectangle {
                                        radius: Theme.radiusSmall
                                        color: Theme.surface
                                        border.color: delegationDaysBox.activeFocus
                                            ? Theme.accent : Theme.border
                                    }
                                    contentItem: TextInput {
                                        text: delegationDaysBox.textFromValue(
                                            delegationDaysBox.value, delegationDaysBox.locale)
                                        color: Theme.text
                                        font: delegationDaysBox.font
                                        horizontalAlignment: Qt.AlignHCenter
                                        verticalAlignment: Qt.AlignVCenter
                                        selectByMouse: true
                                        selectionColor: Theme.accent
                                        selectedTextColor: Theme.accentInk
                                        readOnly: !delegationDaysBox.editable
                                        validator: delegationDaysBox.validator
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    }
                                    up.indicator: Rectangle {
                                        x: delegationDaysBox.width - width
                                        height: delegationDaysBox.height
                                        implicitWidth: delegationDaysBox.fieldHeight
                                        radius: Theme.radiusSmall
                                        color: delegationDaysBox.up.pressed
                                            ? Theme.surfaceAlt : "transparent"
                                        Label {
                                            anchors.centerIn: parent
                                            text: "+"
                                            color: delegationDaysBox.up.hovered
                                                ? Theme.text : Theme.textDim
                                        }
                                    }
                                    down.indicator: Rectangle {
                                        height: delegationDaysBox.height
                                        implicitWidth: delegationDaysBox.fieldHeight
                                        radius: Theme.radiusSmall
                                        color: delegationDaysBox.down.pressed
                                            ? Theme.surfaceAlt : "transparent"
                                        Label {
                                            anchors.centerIn: parent
                                            text: "−"
                                            color: delegationDaysBox.down.hovered
                                                ? Theme.text : Theme.textDim
                                        }
                                    }
                                }
                                Label {
                                    text: "days"
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }
                            // Applied on the button, not on every keystroke: each
                            // change re-issues the delegation, and typing "30"
                            // would issue one for three days on the way.
                            MenuButton {
                                id: saveTermButton
                                Layout.preferredWidth: parent.controlWidth
                                text: "Save the term"
                                enabled: root.session
                                    && delegationDaysBox.value !== root.session.delegationDays
                                onClicked: root.session.delegationDays = delegationDaysBox.value
                            }
                            // The saved term is the truth: a number typed and left
                            // unsaved must not come back on the next open, and one
                            // this account's other devices changed must.
                            Connections {
                                target: root.session
                                function onDelegationDaysChanged() {
                                    delegationDaysBox.value = root.session.delegationDays
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Allow incoming calls"; color: Theme.text }
                            Label {
                                text: "Off, a caller is refused straight away instead of ringing "
                                    + "here. They can still try again later - this can be turned "
                                    + "back on at any time."
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
                            Label { text: "Send read receipts"; color: Theme.text }
                            Label { text: "Lets contacts see a green tick when you receive."; color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        Toggle {
                            checked: root.session ? root.session.sendReceipts : true
                            onToggled: if (root.session) root.session.sendReceipts = checked
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Who is not heard here. A block is per account and reaches its
                // other devices; unblocking does not bring back what was dropped.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Blocked"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    readonly property var rows: (root.session && root.session.contactsRevision >= 0)
                        ? root.session.blockedList() : []
                    Label {
                        visible: parent.rows.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        text: "Nobody. Blocking is offered in a contact's panel; their messages "
                            + "and calls are then dropped as they arrive."
                    }
                    Repeater {
                        model: parent.rows
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.text
                                elide: Text.ElideMiddle
                            }
                            MenuButton {
                                text: "Unblock"
                                onClicked: root.session.setBlocked(modelData.fingerprint, false)
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label { text: "Database"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    MenuButton {
                        Layout.fillWidth: true
                        text: "Change password…"
                        onClicked: {
                            newPass.text = ""
                            newPassAgain.text = ""
                            passwordDialog.open()
                        }
                    }
                    MenuButton { Layout.fillWidth: true; text: "Export encrypted backup…"; onClicked: exportDialog.open() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Sign out"
                            onClicked: { root.close(); App.closeAccount() }
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            // While it runs, it says so: the server has to answer
                            // and the session has to let go of its files. A second
                            // press used to start the whole conversation again.
                            enabled: App.deletingId.length === 0
                            text: App.deletingId.length > 0 ? "Deleting…" : "Delete account…"
                            danger: true
                            onClicked: deleteDialog.show(
                                root.session ? root.session.accountId : "", "")
                        }
                    }
                }
            }
        }
    }

    // The whole account (keys, routing meta and contacts) exports to one
    // password-protected file, named from the display name on this device; the
    // same file restores it.
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
    // Pick an image for the avatar; the app squares and compresses it to within
    // the 500 KB protocol cap before storing and distributing it.
    FileDialog {
        id: avatarDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: if (root.session) { avatarCrop.openFor(selectedFile) }
        // Backing out of the picker is how someone with an avatar asks to have none:
        // the only other reading is that they meant nothing at all, and that is
        // what the dialog asks.
        onRejected: if (root.session && root.session.hasAvatar) { dropAvatarDialog.open() }
    }

    // Picking the part of the picture that becomes the avatar.
    AvatarCropDialog {
        id: avatarCrop
        session: root.session
        onCropped: function(filePath) { if (root.session) { root.session.setAvatar(filePath) } }
    }

    // Remove the account's avatar.
    // Forgetting a device is not a small thing while it is unread: the server
    // stops holding mail for it and drops what it is already holding. It is also
    // not a permanent thing, and saying both is what makes the choice an informed
    // one rather than a scare.
    Dialog {
        id: forgetConfirm
        property string clientId: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(380, root.width - 24)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: "Forget this device?"
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Forget"
            danger: true
            onAccepted: forgetConfirm.accept()
            onRejected: forgetConfirm.reject()
        }
        onAccepted: if (root.session && forgetConfirm.clientId.length > 0) {
            root.session.forgetDevice(forgetConfirm.clientId)
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.bottomMargin: 0
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: "Your server stops keeping mail for " + forgetConfirm.clientId
                    + " and deletes the queue it is holding for it now. Anything in that queue"
                    + " that no other device of yours has collected is gone."
            }
            Label {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.topMargin: 0
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                text: "It is not a ban: the next time that device connects it registers again,"
                    + " appears in this list and works as before."
            }
        }
    }

    Dialog {
        id: dropAvatarDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(360, root.width - 24)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: "Remove your avatar?"
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Remove"
            danger: true
            onAccepted: dropAvatarDialog.accept()
            onRejected: dropAvatarDialog.reject()
        }
        onAccepted: if (root.session) { root.session.clearAvatar() }
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.textDim
            padding: 14
            text: "Your contacts keep the copy they already have until you set a new one."
        }
    }
    // Change the account's own display name. Local only: it updates this device and
    // the name carried in future invite descriptors; existing contacts keep the
    // local name they have for us (it is never sent to them).
    Dialog {
        id: renameSelfDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        // Narrow on purpose: with no width of its own the dialog grew to fit its
        // explanation on one line, which made a two-field form as wide as the app.
        width: Math.min(320, parent ? parent.width - 24 : 320)
        title: "Change name"
        onAccepted: if (root.session) root.session.setDisplayName(renameSelfField.text)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Change name"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        // An account with no name is one the user cannot tell from another, here
        // or on their other devices, so Save has nothing to save.
        footer: DialogButtons {
            acceptText: "Save"
            acceptEnabled: renameSelfField.text.trim().length > 0
            onAccepted: renameSelfDialog.accept()
            onRejected: renameSelfDialog.reject()
        }
        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: renameSelfField
                Layout.fillWidth: true
                placeholderText: "Your name"
                // The name travels in a contact request, whose size is capped by
                // the protocol; the core counts bytes, this counts characters,
                // which is the coarse half of the same limit.
                maximumLength: 64
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
        onAccepted: root.session.exportAccount(root.pendingExportFile, exportPass.text)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Backup password"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { onAccepted: exportPassDialog.accept(); onRejected: exportPassDialog.reject() }
        contentItem: TextField { id: exportPass; echoMode: TextInput.Password; placeholderText: "password"; color: Theme.text; placeholderTextColor: Theme.textDim; implicitWidth: 260; onAccepted: exportPassDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: exportPass.activeFocus ? Theme.accent : Theme.border } }
    }

    // The password this account is kept under at rest. Asked twice, because a
    // mistyped one would lock the account against its owner - only the sealed key
    // beside the database changes, so nothing open on it is disturbed.
    Dialog {
        id: passwordDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Database password"
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Database password"; color: Theme.green
            font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        readonly property bool matched: newPass.text === newPassAgain.text
        onAccepted: root.session.changePassphrase(newPass.text)
        footer: DialogButtons {
            acceptEnabled: passwordDialog.matched
            onAccepted: passwordDialog.accept()
            onRejected: passwordDialog.reject()
        }
        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: newPass
                echoMode: TextInput.Password
                placeholderText: "new password"
                color: Theme.text; placeholderTextColor: Theme.textDim
                Layout.fillWidth: true; implicitWidth: 260
                background: Rectangle { radius: 8; color: Theme.surface
                    border.color: newPass.activeFocus ? Theme.accent : Theme.border }
            }
            TextField {
                id: newPassAgain
                echoMode: TextInput.Password
                placeholderText: "repeat it"
                color: Theme.text; placeholderTextColor: Theme.textDim
                Layout.fillWidth: true; implicitWidth: 260
                onAccepted: if (passwordDialog.matched) { passwordDialog.accept() }
                background: Rectangle { radius: 8; color: Theme.surface
                    border.color: newPassAgain.activeFocus ? Theme.accent : Theme.border }
            }
            Label {
                text: passwordDialog.matched
                    ? (newPass.text.length === 0
                        ? "Empty: this account will be kept unencrypted."
                        : "Asked for whenever this account is opened.")
                    : "The two do not match."
                color: passwordDialog.matched ? Theme.textDim : Theme.warn
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }
    }

    // Full server-connection editor: the same descriptor flow as first connect,
    // reachable any time so an account stuck unconnected can be repaired (re-point
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
            IconButton { iconName: "back"; font.pixelSize: 26; Layout.leftMargin: 8; onClicked: connectionDialog.close() }
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
                // A switched-off account is not going to register with anything:
                // the action saves the endpoint and says so.
                actionText: (root.session && !root.session.online) ? "Save" : "Connect"
                initialFacades: root.session ? root.session.configuredFacades : []
                initialFingerprint: root.session ? root.session.serverFingerprint : ""
                initialReseeds: root.session ? root.session.configuredReseeds : []
                onSubmitted: connectionDialog.close()
            }
        }
    }

    AccountDeleteConfirmDialog {
        id: deleteDialog
        onLocalOnlyRequested: (id) => App.forgetAccountLocally(id)
        onConfirmed: (id) => {
            root.close()
            if (id.length > 0) {
                App.deleteAccount(id)
            }
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
