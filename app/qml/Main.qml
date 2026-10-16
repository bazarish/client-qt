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

    // The dim behind every modal, and the thing that makes it modal: the stock
    // overlay dims but lets a pointer handler underneath still see the press, so a
    // click on a dialog reached the chat behind it. This one swallows the lot.
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
        toast.opacity = 1
        toastTimer.restart()
    }

    // A failed action (i2p activation, a server error, ...) surfaces here on the
    // top layer and stays until dismissed - never covered by another window and
    // never auto-hidden, unlike a toast.
    function showError(message) {
        errorText.text = message
        errorDialog.open()
    }

    // Errors and onboarding info come from whichever account is active; the
    // binding re-targets when the user switches accounts.
    Connections {
        target: App.session
        ignoreUnknownSignals: true
        function onActionFailed(error) { window.showError(error) }
        function onServerHello(reason, message, links) { helloDialog.show(reason, message, links) }
    }

    // The incoming call, announced outside this window: it stands over the desktop
    // while a call rings, unless the user is already looking at the application.
    IncomingCallWindow { mainWindowActive: window.active }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: attachingComponent
    }

    Component { id: attachingComponent; Item { objectName: "attaching" } }
    Component { id: pickerComponent; AccountPicker {} }
    Component { id: mainComponent; MainView {} }

    Connections {
        target: App
        function onAttachedChanged() {
            if (stack.currentItem.objectName === "attaching") {
                stack.replace(null, App.session ? mainComponent : pickerComponent)
            }
        }
        function onAccountOpened() { stack.replace(null, mainComponent) }
        function onAccountOpenFailed(error) { window.showToast(error) }
        // The data moved: the embedded router holds its directory for the life of
        // the process, so there is nothing to do here but say so and stand down.
        function onRestartRequired(message) { restartDialog.show(message) }
        function onCreateFailed(error) { window.showToast(error) }
        // The server would not end the account, so nothing was deleted anywhere.
        function onAccountDeleteFailed(id, error, profileNotOpened) {
            deleteFailedDialog.show(id, error, profileNotOpened)
        }
        // The profile is locked, and its key is what ends the account on the server.
        function onAccountDeleteNeedsUnlock(id, name) { deleteLockedDialog.show(id, name) }
        // A pair link makes a new account, so it goes to the wizard that does.
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
        // "Add account": show the picker over the running session(s).
        function onShowPicker() {
            if (stack.currentItem && stack.currentItem.objectName !== "accountPicker") {
                stack.push(pickerComponent)
            }
        }
        function onSessionChanged() {
            // Last account signed out: back to the picker.
            if (!App.session && stack.depth > 0 && stack.currentItem
                    && stack.currentItem.objectName === "mainView") {
                stack.replace(null, pickerComponent)
            }
        }
    }

    // Lightweight toast for transient messages.
    Rectangle {
        id: toast
        opacity: 0
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 24
        radius: 10
        color: Theme.text
        width: Math.min(toastLabel.implicitWidth + 32, window.width - 48)
        height: toastLabel.implicitHeight + 20
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Label {
            id: toastLabel
            anchors.fill: parent
            anchors.margins: 10
            color: Theme.bg
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.opacity = 0 }
    }

    // Background-activity overlay: a right-edge handle + slide-out panel listing
    // in-flight async operations (contact add, sends, transfers, calls) with live
    // status. Below the error dialog (z 1000), above the app content.
    OperationsOverlay { anchors.fill: parent; z: 900 }

    // Asked once, before anything can start a router, because the answer decides
    // which one starts. Skipping is an answer and is recorded as one, so this
    // appears once and not on every launch.
    GatewayFirstRunDialog {
        id: gatewayFirstRun
        z: 1100
    }
    Component.onCompleted: {
        if (!I2p.gatewayAsked) {
            gatewayFirstRun.open()
        }
        if (backgroundAsk.needed) {
            backgroundAsk.open()
        }
    }

    // What a phone must allow for the app to keep working and ring: asked one at a time.
    Popup {
        id: backgroundAsk
        readonly property bool needed: App.backgroundRestricted || App.fullScreenCallsRestricted
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
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

    // The engine that carries I2P is settled when this application starts, so a
    // different one is a different run. Nothing is left half-switched: every
    // account is closed the moment the choice changes - an open one would go on
    // riding the engine that is no longer chosen - and this stands in front of
    // the window until Bazarish is started again. It cannot be dismissed,
    // because there is nothing useful left to do here.
    Popup {
        id: transportRestart
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
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
            }
            Label {
                text: (I2p.transport === "gateway"
                        ? qsTr("I2P will be carried by the private gateway.")
                        : (I2p.transport === "sam"
                            ? qsTr("I2P will be carried by the local external router.")
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
                // The way out this application has: it lets the accounts close
                // and ends the process itself. Qt.quit() instead unwinds the
                // engine and joins every thread, including one that may be
                // halfway through a request meant to hang - which is a window
                // that does not close when it is told to.
                onClicked: App.prepareForExit()
            }
        }
    }

    // Server onboarding / hello (unregistered-key connect): its own top-layer
    // window with copyable links, dismissed only by its button.
    ServerHelloDialog { id: helloDialog }

    // Generic top-layer error surface (see showError).
    Popup {
        id: errorDialog
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
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
                Layout.fillWidth: true
                text: qsTr("Close")
                hoverEnabled: true
                onClicked: errorDialog.close()
                background: Rectangle { radius: 10; color: parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent }
                contentItem: IconLabel { name: "close"; color: Theme.accentText }
            }
        }
    }

    // Deleting an account has three ways to go, and only one of them is a
    // question for the user (the other two are handled where they happen).
    AccountDeleteLockedDialog {
        id: deleteLockedDialog
        onUnlockRequested: (id) => App.deleteAccountAfterUnlock(id)
        onLocalOnlyRequested: (id) => App.forgetAccountLocally(id)
    }

    // The account's address, when the server and this device disagree about it.
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
        // No way out but out: the data has already moved, every account is
        // closed, and the embedded router still points at the directory that is
        // no longer there. Carrying on in this window would be pretending.
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
}
