// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

WindowPopup {
    id: root
    readonly property var session: App.session
    readonly property var card: root.session ? root.session.publishedCard : ({})
    readonly property bool cardKnown: root.card.issuedAt !== undefined

    readonly property bool overviewFailed: session !== null && session.overviewFailed
    readonly property string awaitingText: overviewFailed ? qsTr("Unknown") : qsTr("Asking your server...")
    function shownColor(fresh, color) {
        return fresh ? color : Theme.textStale
    }
    function day(seconds) {
        return Qt.formatDate(new Date(seconds * 1000), "yyyy-MM-dd")
    }
    function moment(seconds) {
        return Qt.formatDateTime(new Date(seconds * 1000), "yyyy-MM-dd HH:mm")
    }

    onOpened: if (session) { session.watchOverview(true); delegationDaysBox.value = session.delegationDays }
    onClosed: if (session) { session.watchOverview(false) }

    readonly property int kHeldPadding: 10

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Account identifiers")
            showBack: root.returnTo !== null
            onBackClicked: root.goBack()
            onCloseClicked: root.close()
        }

        ScrollView {
            id: identifiersScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: identifiersScroll.availableWidth
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.contentInset
                    spacing: 6
                    Label {
                        text: qsTr("Contact card")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: !root.cardKnown
                        text: qsTr("Not published yet")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    GridLayout {
                        visible: root.cardKnown
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4
                        Label {
                            text: qsTr("Published")
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            text: root.cardKnown ? root.moment(root.card.issuedAt) : ""
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Address")
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            Layout.alignment: Qt.AlignTop
                        }
                        MiddleElided {
                            full: root.cardKnown ? root.card.address : ""
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Mail keys")
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            text: root.cardKnown ? root.card.epochs.map(
                                (epoch) => qsTr("Epoch %1 until %2").arg(epoch.n).arg(root.day(epoch.until)))
                                .join("\n") : ""
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Revoked devices")
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            text: root.cardKnown ? String(root.card.revoked) : ""
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                            Layout.fillWidth: true
                        }
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
                            MiddleElided {
                                visible: root.session && root.session.i2pAddress.length > 0
                                full: root.session ? root.session.i2pAddress : ""
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true
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
                                MiddleElided {
                                    full: root.session ? root.session.i2pServedAddress : ""
                                    color: Theme.warn
                                    font.pixelSize: Theme.fontSmall
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
                    Rectangle {
                        visible: root.session !== null && root.session.accountHeld
                        Layout.fillWidth: true
                        implicitHeight: heldColumn.implicitHeight + 2 * root.kHeldPadding
                        radius: Theme.radiusSmall
                        color: Theme.dangerFill
                        ColumnLayout {
                            id: heldColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: root.kHeldPadding
                            spacing: 4
                            Label {
                                text: qsTr("Account suspended")
                                color: Theme.dangerInk
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label {
                                text: root.session && root.session.approvalNote.length > 0
                                    ? root.session.approvalNote
                                    : qsTr("The server operator suspended this account")
                                color: Theme.dangerInk
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
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
                        columns: identifiersScroll.availableWidth - 2 * Theme.contentInset < sideBySideWidth ? 1 : 2
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
            }
        }
    }

    FileDialog {
        id: i2pKeyDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["I2P destination key (*.dat)", "All files (*)"]
        onAccepted: if (root.session) root.session.loadPersonalKey(selectedFile)
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
}
