// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

ApplicationWindow {
    id: window
    visible: true
    width: 980
    height: 680
    minimumWidth: 360
    minimumHeight: 480
    title: qsTr("Bazarish")
    color: Theme.bg
    LayoutMirroring.enabled: Tr.rightToLeft
    LayoutMirroring.childrenInherit: true

    property bool pageHoldsKeyboard: false
    readonly property real contentInset: pageHoldsKeyboard ? App.keyboardInset : 0
    function noteKeyboard() {
        if (App.keyboardInset === 0) {
            window.pageHoldsKeyboard = false
        } else if (holds(stack, activeFocusItem)) {
            window.pageHoldsKeyboard = true
        }
    }
    onActiveFocusItemChanged: window.noteKeyboard()
    function holds(area, item) {
        for (let at = item; at; at = at.parent) {
            if (at === area) {
                return true
            }
        }
        return false
    }

    // Qt leaves the focus on an item that was hidden, and the keyboard goes on typing into it.
    Connections {
        target: window.activeFocusItem
        function onVisibleChanged() {
            if (!window.activeFocusItem.visible) {
                window.activeFocusItem.focus = false
            }
        }
    }
    // A keyboard put away takes the field's focus with it: kept, the next hand-back raised it again.
    Connections {
        target: App
        function onKeyboardInsetChanged() {
            window.noteKeyboard()
            const field = window.activeFocusItem
            if (App.keyboardInset === 0 && field && !field.readOnly
                    && (field instanceof TextInput || field instanceof TextEdit)) {
                // A SpinBox hands its focus straight back to its editor, so the box itself lets go.
                const owner = field.parent instanceof SpinBox ? field.parent : field
                owner.focus = false
            }
        }
    }

    // The stock modal overlay lets pointer handlers beneath still see the press.
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            preventStealing: true
        }
        WheelHandler { onWheel: (event) => event.accepted = true }
    }

    // Android turns an unhandled back press into closing the window, which would end the app.
    onClosing: function(close) {
        if (App.mobile) {
            close.accepted = false
            window.goBack()
        }
    }
    function goBack() {
        if (App.closeTopPopup(window)) {
            return
        }
        if (stack.depth > 1) {
            stack.pop()
            return
        }
        if (stack.currentItem && stack.currentItem.goBack && stack.currentItem.goBack()) {
            return
        }
        App.moveToBackground()
    }

    function showToast(message) {
        toastLabel.text = message
        toast.open()
        toastTimer.restart()
    }

    function showError(message) {
        errorText.text = message
        errorDialog.open()
    }

    Connections {
        target: App.session
        ignoreUnknownSignals: true
        function onActionFailed(error) { window.showError(error) }
        function onServerHello(reason, message, links) { helloDialog.show(reason, message, links) }
    }

    IncomingCallWindow { mainWindowActive: window.active }

    StackView {
        id: stack
        anchors.fill: parent
        anchors.bottomMargin: window.contentInset
        // An item that takes a press does not keep it from the pointer handlers of the items below.
        enabled: !operations.open
        initialItem: attachingComponent
        replaceEnter: null
        replaceExit: null
    }

    Component { id: attachingComponent; Item { objectName: "attaching" } }
    Component { id: pickerComponent; AccountPicker {} }
    Component { id: mainComponent; MainView {} }

    Connections {
        target: App
        function onAttachedChanged() {
            if (!App.attached) {
                stack.replace(null, attachingComponent)
                return
            }
            if (stack.currentItem.objectName === "attaching") {
                stack.replace(null, App.session ? mainComponent : pickerComponent)
            }
            if (!I2p.gatewayAsked) {
                gatewayFirstRun.open()
            }
        }
        function onAccountOpened() {
            if (stack.currentItem.objectName !== "mainView") {
                stack.replace(null, mainComponent)
            }
        }
        function onAccountOpenFailed(error) { window.showToast(error) }
        function onRestartRequired(message) { restartDialog.show(message) }
        function onCreateFailed(error) { window.showToast(error) }
        function onImportRefused(kind) { importRefusedDialog.show(kind) }
        function onAccountDeleteFailed(id, error, profileNotOpened) {
            deleteFailedDialog.show(id, error, profileNotOpened)
        }
        function onAccountDeleteNeedsUnlock(id, name) { deleteLockedDialog.show(id, name) }
        function onPairLinkOpened(link) {
            let page = stack.currentItem
            if (page && page.objectName === "createWizard") {
                page.pairLink = link
                return
            }
            if (page.objectName === "attaching") {
                page = stack.replace(null, pickerComponent)
            } else if (page.objectName !== "accountPicker") {
                page = stack.push(pickerComponent)
            }
            page.openPairing(link)
        }
        function onLinkRefused(reason) { window.showToast(reason) }
        function onShowPicker() {
            if (stack.currentItem && stack.currentItem.objectName !== "accountPicker") {
                stack.push(pickerComponent)
            }
        }
        function onSessionChanged() {
            if (!App.session && stack.depth > 0 && stack.currentItem
                    && stack.currentItem.objectName === "mainView") {
                stack.replace(null, pickerComponent)
            }
        }
    }

    // A popup, so it leaves the overlay when closed: any lasting child keeps the overlay visible, and that turns the swipes off.
    Popup {
        id: toast
        parent: Overlay.overlay
        z: 950
        closePolicy: Popup.NoAutoClose
        x: (parent.width - width) / 2
        y: parent.height - height - 24 - window.contentInset
        width: Math.min(toastLabel.implicitWidth + 32, window.width - 48)
        leftPadding: 16
        rightPadding: 16
        topPadding: 10
        bottomPadding: 10
        background: Rectangle { radius: 10; color: Theme.text }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 200 } }
        contentItem: Label {
            id: toastLabel
            color: Theme.bg
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.close() }
    }

    OperationsOverlay {
        id: operations
        anchors.fill: parent
        anchors.bottomMargin: window.contentInset
        z: 900
    }

    GatewayFirstRunDialog {
        id: gatewayFirstRun
        z: 1100
    }
    Component.onCompleted: {
        App.forgetFocusOnClose(Overlay.overlay)
        App.paintSystemBars(Theme.bg)
        if (backgroundAsk.needed) {
            backgroundAsk.open()
        }
    }
    Connections {
        target: Theme
        function onLightChanged() { App.paintSystemBars(Theme.bg) }
    }

    Popup {
        id: backgroundAsk
        readonly property bool needed: App.backgroundRestricted || App.fullScreenCallsRestricted
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        z: 1050
        width: Math.min(420, (Overlay.overlay ? Overlay.overlay.width : 420) - 32)
        padding: 14
        background: DialogFrame { }
        Connections {
            target: App
            function onBackgroundRestrictedChanged() {
                if (!backgroundAsk.needed) {
                    backgroundAsk.close()
                }
            }
        }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: App.backgroundRestricted
                        ? qsTr("Allow unrestricted background work to get notifications.")
                        : qsTr("Allow full-screen notifications to see incoming calls on the lock screen.")
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                IconButton {
                    iconName: "close"
                    Layout.alignment: Qt.AlignTop
                    onClicked: backgroundAsk.close()
                }
            }
            MenuButton {
                text: qsTr("Settings")
                Layout.alignment: Qt.AlignRight
                onClicked: App.backgroundRestricted ? App.openBackgroundSettings()
                                                    : App.openFullScreenCallsSettings()
            }
        }
    }

    Popup {
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        z: 1200
        closePolicy: Popup.NoAutoClose
        visible: I2p.restartNeeded
        width: Math.min(420, (Overlay.overlay ? Overlay.overlay.width : 420) - 32)
        padding: 18
        background: DialogFrame { }
        onVisibleChanged: if (visible) { App.closeAllSessions() }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: qsTr("Start Bazarish again")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                text: (I2p.transport === "gateway"
                        ? qsTr("I2P will be carried by the external gateway.")
                        : (I2p.transport === "sam"
                            ? qsTr("I2P will be carried by an external router.")
                            : qsTr("I2P will be carried by the engine inside this application.")))
                    + " "
                    + qsTr("The I2P backend is chosen at startup. Your accounts are closed; restart to apply.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            MenuButton {
                iconName: "close"
                text: qsTr("Quit")
                Layout.alignment: Qt.AlignRight
                // Not Qt.quit(): it joins every thread, one of them maybe inside a request meant to hang.
                onClicked: App.prepareForExit()
            }
        }
    }

    ServerHelloDialog { id: helloDialog }

    Popup {
        id: errorDialog
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        z: 1000
        closePolicy: Popup.CloseOnEscape
        width: Math.min(460, (Overlay.overlay ? Overlay.overlay.width : 460) - 32)
        padding: 18
        background: DialogFrame { }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Something went wrong")
                    color: Theme.danger; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                IconButton { iconName: "close"; onClicked: errorDialog.close() }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, errorText.implicitHeight + 16)
                TextArea {
                    id: errorText
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: Theme.text
                    selectByMouse: true
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                }
            }
            Button {
                focusPolicy: Theme.pressFocus
                Layout.fillWidth: true
                text: qsTr("Close")
                hoverEnabled: true
                onClicked: errorDialog.close()
                background: Rectangle { radius: 10; color: parent.hovered ? Theme.accentHover : Theme.accent }
                contentItem: IconLabel { name: "close"; color: Theme.accentText }
            }
        }
    }

    AccountDeleteLockedDialog {
        id: deleteLockedDialog
        onUnlockRequested: (id) => App.deleteAccountAfterUnlock(id)
        onLocalOnlyRequested: (id) => App.forgetAccountLocally(id)
    }

    AddressChoiceDialog { id: addressChoiceDialog }

    Connections {
        target: App.session
        ignoreUnknownSignals: true
        function onAddressNeedsChoice(servedHost, ourHost) {
            addressChoiceDialog.show(servedHost, ourHost)
        }
    }

    AccountDeleteFailedDialog {
        id: deleteFailedDialog
        onRetryRequested: (id) => App.deleteAccount(id)
        onLocalOnlyRequested: (id) => App.forgetAccountLocally(id)
    }

    Dialog {
        id: restartDialog
        property string message: ""
        function show(text) { message = text; open() }
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: DialogFrame { destructive: true }
        header: Label {
            text: qsTr("Restart Bazarish")
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: qsTr("Quit")
            showReject: false
            onAccepted: restartDialog.accept()
        }
        onAccepted: Qt.quit()
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.text
            padding: 14
            text: restartDialog.message
        }
    }

    Dialog {
        id: importRefusedDialog
        property string kind: ""
        function show(accountKind) { kind = accountKind; open() }
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: DialogFrame { destructive: true }
        header: Label {
            text: qsTr("Account not added")
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: qsTr("OK")
            showReject: false
            onAccepted: importRefusedDialog.accept()
        }
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.text
            padding: 14
            text: importRefusedDialog.kind === "bot"
                ? qsTr("This account was created as a bot.")
                : qsTr("This account was created as a group.")
        }
    }

    FocusSelection { appWindow: window }
}
