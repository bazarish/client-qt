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
    title: "Bazarish"
    color: Theme.bg

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

    StackView {
        id: stack
        anchors.fill: parent
        // Restored session(s) from last run -> straight into the app, no dialog.
        initialItem: App.session ? mainComponent : pickerComponent
    }

    Component { id: pickerComponent; AccountPicker {} }
    Component { id: mainComponent; MainView {} }

    Connections {
        target: App
        function onAccountOpened() { stack.replace(null, mainComponent) }
        function onAccountOpenFailed(error) { window.showToast(error) }
        // The data moved: the embedded router holds its directory for the life of
        // the process, so there is nothing to do here but say so and stand down.
        function onRestartRequired(message) { restartDialog.show(message) }
        function onCreateFailed(error) { window.showToast(error) }
        // The server would not end the account, so nothing was deleted anywhere.
        function onAccountDeleteFailed(id, error) { deleteFailedDialog.show(id, error) }
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
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.danger; border.width: 2 }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Something went wrong"
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
                text: "Close"
                hoverEnabled: true
                onClicked: errorDialog.close()
                background: Rectangle { radius: 10; color: parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent }
                contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }

    // A deletion the server refused or could not answer. Nothing has been
    // removed: the profile is what holds the key that can ask again, so the
    // choice between trying later and walking away belongs to the user.
    Dialog {
        id: deleteFailedDialog
        property string accountId: ""
        property string reason: ""
        function show(id, error) { accountId = id; reason = error; open() }
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(400, parent ? parent.width - 24 : 400)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
        header: Label {
            text: "The account was not deleted"
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Try again"
            rejectText: "Close"
            onAccepted: deleteFailedDialog.accept()
            onRejected: deleteFailedDialog.reject()
        }
        onAccepted: App.deleteAccount(deleteFailedDialog.accountId)
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.bottomMargin: 0
                wrapMode: Text.Wrap
                color: Theme.text
                text: "Your server did not end the account: " + deleteFailedDialog.reason
                    + "\n\nNothing was deleted. Try again when the server is reachable - "
                    + "this profile holds the only key that can ask it to."
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.bottomMargin: 4
                wrapMode: Text.Wrap
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                text: "Remove from this device anyway"
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        App.forgetAccountLocally(deleteFailedDialog.accountId)
                        deleteFailedDialog.close()
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.bottomMargin: 10
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: "The account would go on existing on the server - address, mailbox and all - "
                    + "with no key left anywhere to end it."
            }
        }
    }

    Dialog {
        id: restartDialog
        property string message: ""
        function show(text) { message = text; open() }
        anchors.centerIn: Overlay.overlay
        modal: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
        header: Label {
            text: "Restart Bazarish"
            color: Theme.neon
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        // No way out but out: the data has already moved, every account is
        // closed, and the embedded router still points at the directory that is
        // no longer there. Carrying on in this window would be pretending.
        footer: DialogButtons {
            acceptText: "Quit"
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
