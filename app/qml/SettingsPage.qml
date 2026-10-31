// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish
import "Format.js" as Format

Popup {
    id: root
    bottomMargin: activeFocus ? App.keyboardInset : 0
    property var session: null
    signal showInvite()
    signal showSignWithKey()
    signal showStorage()
    signal showRouterStatus()
    signal showConnectionLog()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    height: Math.min(parent ? parent.height - 40 : 600, 640)
    padding: 0

    onOpened: if (session) { session.refreshI2pStatus(); session.refreshStorageUsage();
        session.refreshDevices(); delegationDaysBox.value = session.delegationDays }

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

    background: DialogFrame { }

    property string pendingExportFile: ""

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label { text: qsTr("Account"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

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
                    Layout.margins: 16
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
                                onTapped: App.mobile && App.canPickPictures
                                    ? App.pickAvatar() : avatarDialog.open()
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
                            Label {
                                id: fingerprintLine
                                property bool copied: false
                                text: copied
                                    ? qsTr("Copied to clipboard")
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
                                        App.copyText(root.session.fingerprint)
                                        fingerprintLine.copied = true
                                        copiedTimer.restart()
                                    }
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                        IconButton {
                            iconName: "link"
                            Layout.alignment: Qt.AlignVCenter
                            onClicked: { root.close(); root.showInvite() }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            iconName: "key"
                            text: qsTr("Sign with the account key")
                            onClicked: { root.close(); root.showSignWithKey() }
                        }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
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
                            color: (root.session && root.session.reachable) ? Theme.success : Theme.warn
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.session && root.session.activeFacadeHost.length > 0
                                ? (root.session.reachable
                                    ? qsTr("via %1").arg(root.session.activeFacadeHost)
                                    : qsTr("connecting via %1").arg(root.session.activeFacadeHost))
                                : ""
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle
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
                        readonly property double used: info.mailboxUsed !== undefined
                            ? info.mailboxUsed : 0
                        readonly property double quota: info.mailboxQuota !== undefined
                            ? info.mailboxQuota : 0
                        Rectangle {
                            id: storageBox
                            Layout.fillWidth: true
                            radius: Theme.radiusSmall
                            color: "transparent"
                            implicitHeight: storageRow.implicitHeight + 10
                            SequentialAnimation {
                                id: storageFlash
                                PropertyAction { target: storageBox; property: "color"
                                    value: Qt.rgba(0.12, 0.48, 0.08, 0.5) }
                                PauseAnimation { duration: 550 }
                                ColorAnimation { target: storageBox; property: "color"
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
                                        text: qsTr("Server storage usage")
                                        color: Theme.textDim
                                        font.pixelSize: Theme.fontSmall
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                    }
                                    Label {
                                        text: storageSection.ok
                                            ? (Format.bytes(storageSection.used) + " / "
                                                + Format.bytes(storageSection.quota))
                                            : qsTr("unavailable")
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
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.session && root.session.connected

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Devices on this account")
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
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
                                    text: modelData.clientId
                                        + (modelData.queue > 0
                                            ? qsTr(" (queue: %1)").arg(modelData.queue) : "")
                                    color: modelData.current ? Theme.text : Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: qsTr("(this device)")
                                    visible: modelData.current
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                }
                                Label {
                                    text: qsTr("Forget")
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
                            text: qsTr("Nothing yet - open this while connected to ask your server.")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
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
                        iconName: "devices"
                        text: qsTr("Ask my other devices for contacts")
                        enabled: root.session !== null
                        onClicked: root.session.askForContacts()
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label {
                        text: qsTr("Your I2P destination")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Connections {
                        target: root.session
                        ignoreUnknownSignals: true
                        function onI2pAnswered() { i2pFlash.restart() }
                    }
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
                                readonly property bool delegated: root.session
                                    && root.session.i2pTransientExpires > 0
                                readonly property string serverState: root.session
                                    ? root.session.i2pServerState : ""
                                text: (delegated
                                        ? qsTr("Delegated until %1").arg(Qt.formatDate(
                                            new Date(root.session.i2pTransientExpires * 1000),
                                            "yyyy-MM-dd"))
                                        : qsTr("Not delegated"))
                                    + " \u00b7 " + (serverState === "active"
                                        ? qsTr("published")
                                        : (serverState === "building"
                                            ? qsTr("publishing")
                                            : (serverState === "expired"
                                                ? qsTr("expired")
                                                : (serverState.length > 0
                                                    ? qsTr("nothing on the server")
                                                    : qsTr("no answer from the server")))))
                                color: serverState === "active" ? Theme.success : Theme.warn
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: root.session && root.session.i2pAddress.length > 0
                                text: root.session ? root.session.i2pAddress : ""
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                            Label {
                                visible: root.session && !root.session.i2pHasKey
                                text: qsTr("No destination key on this account yet.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            ColumnLayout {
                                visible: root.session && root.session.i2pAddressMismatch
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: qsTr("Your server serves another address")
                                    color: Theme.warn
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    text: root.session ? root.session.i2pServedAddress : ""
                                    color: Theme.warn
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: qsTr("Another device of yours published it. Until the two agree, some mail reaches nobody.")
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                MenuButton {
                                    iconName: "broadcast"
                                    text: qsTr("Publish this device's address")
                                    enabled: root.session && root.session.connected
                                    onClicked: root.session.keepThisDeviceAddress()
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                    ButtonRow {
                        visible: root.session && !root.session.i2pHasKey
                        Layout.fillWidth: true
                        MenuButton { Layout.fillWidth: true; iconName: "plus"; text: qsTr("Create address")
                            onClicked: root.session.generatePersonalKey() }
                        MenuButton { Layout.fillWidth: true; iconName: "folder"; text: qsTr("Load an existing key"); onClicked: i2pKeyDialog.open() }
                    }
                    ButtonRow {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && !root.session.i2pEnabled
                            iconName: "broadcast"
                            text: root.session && root.session.i2pBusy ? qsTr("Publishing\u2026")
                                : qsTr("Publish address")
                            enabled: root.session && root.session.connected
                                && !root.session.i2pBusy
                            onClicked: root.session.publishPersonalDest()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && root.session.i2pEnabled
                            iconName: "power"
                            text: root.session && root.session.i2pBusy ? qsTr("Taking offline\u2026")
                                : qsTr("Take offline")
                            enabled: root.session && !root.session.i2pBusy
                            onClicked: takeOfflineDialog.open()
                        }
                    }
                    MenuButton {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true
                        iconName: "refresh"
                        text: root.session && root.session.i2pBusy
                            ? qsTr("Publishing…") : qsTr("Renew")
                        enabled: root.session && root.session.connected
                            && !root.session.i2pBusy
                        onClicked: root.session.publishPersonalDest()
                    }
                    MenuButton {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true
                        iconName: "refresh"
                        text: qsTr("New key")
                        danger: true
                        enabled: root.session && root.session.connected
                            && !root.session.i2pBusy
                        onClicked: newKeyDialog.open()
                    }
                    GridLayout {
                        id: delegationRow
                        Layout.fillWidth: true
                        columnSpacing: 8
                        rowSpacing: 8
                        readonly property int sideBySideWidth: 400
                        columns: width < sideBySideWidth ? 1 : 2
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Delegation term"); color: Theme.text
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("Sets how long your server keeps serving incoming mail while you are offline.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        ColumnLayout {
                            Layout.alignment: delegationRow.columns === 1
                                ? Qt.AlignLeft : Qt.AlignVCenter
                            Layout.rightMargin: 2
                            spacing: 6
                            // An editable SpinBox asks for far more room than it needs.
                            readonly property int controlWidth: 168
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
                                    // Fires on a step or when typing ends, so "30" never passes through 3 days.
                                    onValueModified: if (root.session) {
                                        root.session.delegationDays = value
                                    }
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
                                    text: qsTr("days")
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fontSmall
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }
                            Connections {
                                target: root.session
                                function onDelegationDaysChanged() {
                                    delegationDaysBox.value = root.session.delegationDays
                                }
                            }
                        }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
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
                                text: qsTr("Off, a caller is refused automatically.")
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
                            Label { text: qsTr("Lets contacts see a green mark when you receive."); color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true }
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
                            Label { text: qsTr("Contacts see when the app is open and when you type, and you see theirs."); color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true }
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
                    Layout.margins: 16
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
                    MenuButton { Layout.fillWidth: true; iconName: "save"; text: qsTr("Back up this account"); onClicked: exportDialog.open() }
                    MenuButton {
                        Layout.fillWidth: true
                        iconName: "devices"
                        text: qsTr("Connect a new device online")
                        onClicked: pairDeviceSheet.open()
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
                    Layout.margins: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MenuButton {
                            Layout.fillWidth: true
                            enabled: App.deletingId.length === 0
                            iconName: "trash"
                            text: App.deletingId.length > 0 ? qsTr("Deleting…") : qsTr("Delete account")
                            danger: true
                            onClicked: deleteDialog.show(
                                root.session ? root.session.accountId : "", "")
                        }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
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
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.text
                                elide: Text.ElideMiddle
                            }
                            MenuButton {
                                iconName: "check"
                                text: qsTr("Unblock")
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
    FileDialog {
        id: i2pKeyDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["I2P destination key (*.dat)", "All files (*)"]
        onAccepted: if (root.session) root.session.loadPersonalKey(selectedFile)
    }
    FileDialog {
        id: avatarDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: if (root.session) { avatarCrop.openFor(selectedFile) }
        onRejected: if (root.session && root.session.hasAvatar) { dropAvatarDialog.open() }
    }

    Connections {
        target: App
        function onAvatarPicked(uri) {
            if (uri.length > 0) {
                avatarCrop.openFor(uri)
            } else if (root.session && root.session.hasAvatar) {
                dropAvatarDialog.open()
            }
        }
    }

    AvatarCropDialog {
        id: avatarCrop
        session: root.session
        onCropped: function(grab) { if (root.session) { root.session.setAvatarFromGrab(grab) } }
    }

    Dialog {
        id: forgetConfirm
        property string clientId: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(380, root.width - 24)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Forget this device?")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: qsTr("Forget")
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
                text: qsTr("Your server stops keeping mail for %1 and drops the queue it holds. Anything in it that no other device collected is gone.").arg(forgetConfirm.clientId)
            }
            Label {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.topMargin: 0
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                text: qsTr("The device registers again the next time it connects.")
            }
        }
    }

    Dialog {
        id: takeOfflineDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(380, root.width - 24)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Take offline?")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: qsTr("Take offline")
            danger: true
            onAccepted: takeOfflineDialog.accept()
            onRejected: takeOfflineDialog.reject()
        }
        onAccepted: if (root.session) { root.session.disablePersonalDest() }
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.textDim
            padding: 14
            text: qsTr("This removes your personal I2P address from the server. Your account becomes unreachable for other users. It makes sense if you plan to change servers and are withdrawing your address to publish it in the new place.")
        }
    }

    Dialog {
        id: dropAvatarDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, root.width - 24)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Remove your avatar?")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
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
            text: qsTr("Your contacts keep the copy they already have until you set a new one.")
        }
    }
    Dialog {
        id: renameSelfDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        // Without a width of its own the dialog grows to fit its text on one line.
        width: Math.min(320, parent ? parent.width - 24 : 320)
        title: qsTr("Change name")
        onAccepted: if (root.session) root.session.setDisplayName(renameSelfField.text)
        background: DialogFrame { }
        header: Label { text: qsTr("Change name"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
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
                // Characters here; the core enforces the contact request's byte cap.
                maximumLength: 64
                color: Theme.text
                placeholderTextColor: Theme.textDim
                onAccepted: renameSelfDialog.accept()
                background: Rectangle { radius: 8; color: Theme.surface; border.color: renameSelfField.activeFocus ? Theme.accent : Theme.border }
            }
            Label {
                text: qsTr("Only updates this device and your invite link.")
                color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }
    }

    Dialog {
        id: exportPassDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        title: qsTr("Backup password")
        onAccepted: root.session.exportAccount(root.pendingExportFile, exportPass.text)
        background: DialogFrame { }
        header: Label { text: qsTr("Backup password"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { onAccepted: exportPassDialog.accept(); onRejected: exportPassDialog.reject() }
        contentItem: TextField { id: exportPass; echoMode: TextInput.Password; placeholderText: qsTr("password"); color: Theme.text; placeholderTextColor: Theme.textDim; implicitWidth: 260; onAccepted: exportPassDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: exportPass.activeFocus ? Theme.accent : Theme.border } }
    }

    Dialog {
        id: passwordDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        title: qsTr("Database password")
        background: DialogFrame { }
        header: Label { text: qsTr("Database password"); color: Theme.green
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
                placeholderText: qsTr("new password")
                color: Theme.text; placeholderTextColor: Theme.textDim
                Layout.fillWidth: true; implicitWidth: 260
                background: Rectangle { radius: 8; color: Theme.surface
                    border.color: newPass.activeFocus ? Theme.accent : Theme.border }
            }
            TextField {
                id: newPassAgain
                echoMode: TextInput.Password
                placeholderText: qsTr("repeat it")
                color: Theme.text; placeholderTextColor: Theme.textDim
                Layout.fillWidth: true; implicitWidth: 260
                onAccepted: if (passwordDialog.matched) { passwordDialog.accept() }
                background: Rectangle { radius: 8; color: Theme.surface
                    border.color: newPassAgain.activeFocus ? Theme.accent : Theme.border }
            }
            Label {
                text: passwordDialog.matched
                    ? (newPass.displayText.length === 0
                        ? qsTr("Empty: this account will be kept unencrypted.")
                        : qsTr("Asked for whenever this account is opened."))
                    : qsTr("The two do not match.")
                color: passwordDialog.matched ? Theme.textDim : Theme.warn
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
        width: Math.min(460, (Overlay.overlay ? Overlay.overlay.width : 460) - 2 * Theme.spacing)
        margins: Theme.spacing
        bottomMargin: Theme.spacing + (activeFocus ? App.keyboardInset : 0)
        onOpened: connectionForm.reset()
        background: DialogFrame { }
        header: RowLayout {
            spacing: 4
            IconButton { iconName: "back"; Layout.leftMargin: 8; onClicked: connectionDialog.close() }
            Label {
                text: qsTr("Server connection")
                color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
                Layout.fillWidth: true; Layout.rightMargin: 14; topPadding: 14; bottomPadding: 14
                wrapMode: Text.Wrap
            }
        }
        contentItem: ScrollColumn {
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
        width: Math.min(360, (Overlay.overlay ? Overlay.overlay.width : 360) - 2 * Theme.spacing)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Server connection")
            color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
            padding: 14
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
                onClicked: { serverChoice.close(); serverShare.open() }
            }
        }
    }
    ShareLinkSheet {
        id: serverShare
        title: qsTr("Share server")
        note: qsTr("This link connects another account to your server.")
        link: root.session ? root.session.serverLink : ""
        hasBack: true
        onBack: { serverShare.close(); serverChoice.open() }
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
        id: newKeyDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, (Overlay.overlay ? Overlay.overlay.width : 360) - 2 * Theme.spacing)
        title: qsTr("New key")
        footer: DialogButtons { acceptText: qsTr("New key"); danger: true; onAccepted: newKeyDialog.accept(); onRejected: newKeyDialog.reject() }
        onAccepted: if (root.session) root.session.replacePersonalKey()
        background: DialogFrame { destructive: true }
        header: Label { text: qsTr("New key"); color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        contentItem: Label {
            text: qsTr("Your address changes. Every invite link you have handed out stops working and the old address stops answering at once. Your contacts are sent the new details automatically, and your aliases are repointed by themselves. This helps in the rare case of shaking off spam. Do not take this step unless you are sure why you need it.")
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    PairDeviceSheet {
        id: pairDeviceSheet
        session: root.session
    }
}
