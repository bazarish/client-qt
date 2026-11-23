// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

WindowPopup {
    id: root
    bottomMargin: activeFocus ? App.keyboardInset : 0
    property var session: null
    signal showInvite()
    signal showSignWithKey()
    signal showStorage()
    signal showConnectionLog()

    readonly property bool overviewFailed: session !== null && session.overviewFailed
    readonly property string awaitingText: overviewFailed ? qsTr("Unknown") : qsTr("Asking your server...")
    function shownColor(fresh, color) {
        return fresh ? color : Theme.textStale
    }

    onOpened: if (session) { session.watchOverview(true); delegationDaysBox.value = session.delegationDays }
    onClosed: if (session) { session.watchOverview(false) }

    property string pendingExportFile: ""
    property bool pendingExportPrimary: true
    readonly property int kDeviceActionSize: 22
    readonly property int kDevicesActionGap: 10
    readonly property int kDeviceActionIcon: 14

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
                        Label {
                            Layout.fillWidth: true
                            text: root.session && root.session.activeFacadeHost.length > 0
                                ? (root.session.reachable
                                    ? root.session.activeFacadeHost
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
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.kDeviceActionSize
                            spacing: 8
                            Fingerprint {
                                value: deviceRow.modelData.deviceId
                                whole: true
                                color: deviceRow.ink
                                font.pixelSize: Theme.fontSmall
                            }
                            RowLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Label {
                                    text: deviceRow.modelData.primary ? qsTr("Primary") : qsTr("Secondary")
                                    color: deviceRow.ink
                                    font.pixelSize: Theme.fontSmall
                                }
                                Label {
                                    readonly property string status: deviceRow.modelData.revoked ? qsTr("Revoked")
                                        : (deviceRow.modelData.current ? qsTr("(this device)")
                                            : (deviceRow.modelData.departed ? qsTr("(removed from that device)")
                                                : (!deviceRow.modelData.registered ? qsTr("(inactive)")
                                                    : (deviceRow.showsQueue
                                                        ? "(" + qsTr("message queue: %1").arg(deviceRow.modelData.queue)
                                                            + (deviceRow.manages ? " \u2013 " : ")")
                                                        : ""))))
                                    text: status.length > 0 ? " " + status : ""
                                    color: deviceRow.showsQueue ? deviceRow.queueInk : deviceRow.ink
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }
                                Label {
                                    visible: deviceRow.showsQueue && deviceRow.manages
                                    text: qsTr("Clear")
                                    color: clearArea.containsMouse ? Theme.danger : deviceRow.queueInk
                                    font.pixelSize: Theme.fontSmall
                                    font.underline: true
                                    MouseArea {
                                        id: clearArea
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            clearConfirm.deviceId = deviceRow.modelData.deviceId
                                            clearConfirm.open()
                                        }
                                    }
                                }
                                Label {
                                    visible: deviceRow.showsQueue && deviceRow.manages
                                    text: ")"
                                    color: deviceRow.queueInk
                                    font.pixelSize: Theme.fontSmall
                                }
                                Item { Layout.fillWidth: true }
                            }
                            IconButton {
                                visible: deviceRow.manages
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
                    Label {
                        text: qsTr("Your I2P destination")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: i2pStatusCol.implicitHeight + 12
                        ColumnLayout {
                            id: i2pStatusCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 6
                            spacing: 4
                            Label {
                                visible: !(root.session && root.session.overviewKnown)
                                text: root.awaitingText
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: root.session !== null && root.session.overviewKnown
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
                                color: root.shownColor(root.session !== null && root.session.overviewFresh,
                                    serverState === "active" ? Theme.success : Theme.warn)
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: root.session && root.session.i2pAddress.length > 0
                                text: root.session ? root.session.i2pAddress : ""
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall; elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                            RowLayout {
                                visible: root.session !== null && root.session.epochReserveLow
                                Layout.fillWidth: true
                                spacing: 6
                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    implicitWidth: 8; implicitHeight: 8; radius: 4
                                    color: Theme.danger
                                }
                                Label {
                                    text: root.session ? qsTr("Mail keys until %1").arg(Qt.formatDate(
                                        new Date(root.session.epochReserveUntil * 1000), "yyyy-MM-dd"))
                                        : ""
                                    color: Theme.danger
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                visible: root.session && root.session.i2pAddress.length === 0
                                text: qsTr("No destination key on this account yet")
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
                        visible: root.session && root.session.i2pAddress.length === 0
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
                            text: root.session && root.session.i2pBusy ? qsTr("Publishing...")
                                : qsTr("Publish address")
                            enabled: root.session && root.session.connected
                                && !root.session.i2pBusy && root.session.primaryDevice
                            onClicked: root.session.publishPersonalDest()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            visible: root.session && root.session.i2pEnabled
                            iconName: "power"
                            text: root.session && root.session.i2pBusy ? qsTr("Taking offline...")
                                : qsTr("Take offline")
                            enabled: root.session && !root.session.i2pBusy
                                && root.session.primaryDevice
                            onClicked: takeOfflineDialog.open()
                        }
                    }
                    MenuButton {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true
                        iconName: "refresh"
                        text: root.session && root.session.i2pBusy
                            ? qsTr("Publishing...") : qsTr("Renew")
                        enabled: root.session && root.session.connected
                            && !root.session.i2pBusy && root.session.primaryDevice
                        onClicked: root.session.renewPersonalDest()
                    }
                    MenuButton {
                        visible: root.session && root.session.i2pHasKey
                        Layout.fillWidth: true
                        iconName: "refresh"
                        text: qsTr("New key")
                        danger: true
                        enabled: root.session && root.session.connected
                            && !root.session.i2pBusy && root.session.primaryDevice
                        onClicked: newKeyDialog.open()
                    }
                    GridLayout {
                        id: delegationRow
                        Layout.fillWidth: true
                        columnSpacing: 8
                        rowSpacing: 8
                        readonly property int sideBySideWidth: 400
                        columns: settingsScroll.availableWidth - 2 * Theme.contentInset < sideBySideWidth ? 1 : 2
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Delegation term"); color: Theme.text
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("Sets how long your server keeps serving incoming mail while you are offline")
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
                                    enabled: root.session !== null && root.session.primaryDevice
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
                                        color: Theme.control
                                        border.color: delegationDaysBox.activeFocus
                                            ? Theme.accent : Theme.controlEdge
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
                            Layout.fillWidth: true
                            enabled: App.deletingId.length === 0 && root.session !== null
                            iconName: "trash"
                            text: App.deletingId.length > 0 ? qsTr("Deleting...") : qsTr("Delete account")
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
        function onAvatarPickFailed(reason) { window.showToast(reason) }
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
                    .arg(clearConfirm.deviceId)
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
        id: takeOfflineDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(Theme.dialogWidth, root.width - Theme.windowSideRoom)
        background: DialogFrame { }
        header: WindowHeader {
            title: qsTr("Take offline?")
            showClose: false
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
            text: qsTr("Your contacts keep the copy they already have until you set a new one")
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
                text: qsTr("Only updates this device and your invite link")
                color: Theme.textDim; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; Layout.fillWidth: true
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
            App.forgetAccountLocally(id)
        }
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
        width: Math.min(Theme.dialogWidth, (Overlay.overlay ? Overlay.overlay.width : Theme.dialogWidth) - Theme.windowSideRoom)
        title: qsTr("New key")
        footer: DialogButtons { acceptText: qsTr("New key"); danger: true; onAccepted: newKeyDialog.accept(); onRejected: newKeyDialog.reject() }
        onAccepted: if (root.session) root.session.replacePersonalKey()
        background: DialogFrame { destructive: true }
        header: WindowHeader {
            title: newKeyDialog.title
            showClose: false
        }
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
