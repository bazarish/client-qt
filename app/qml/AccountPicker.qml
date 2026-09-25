import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "accountPicker"

    Component { id: wizardComponent; CreateAccountWizard {} }

    property string pendingId: ""
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""
    property string rowFingerprint: ""

    // When the picker was pushed over a running app (the user is just switching
    // account), pop it at once on selection so the extra window does not linger;
    // the active session swaps underneath. On first launch (nothing open yet)
    // there is nothing to return to, so the open transition replaces it instead.
    function closeIfSwitching() {
        const view = root.StackView.view
        if (App.hasOpenAccounts && view && view.depth > 1) {
            view.pop()
        }
    }

    function copyFingerprint(fp) {
        App.copyText(fp)
        if (typeof window !== "undefined") window.showToast("Fingerprint copied")
    }

    // The application's own settings, reachable before any account is open: the
    // I2P router status lives behind them, and a client stuck building tunnels
    // has nowhere else to look.
    IconButton {
        iconName: "gear"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        onClicked: appSettings.open()
    }
    RouterStatusPage { id: routerStatus; onBack: { routerStatus.close(); appSettings.open() } }
    AppSettingsPage {
        id: appSettings
        parent: Overlay.overlay
        anchors.centerIn: parent
        onShowRouterStatus: routerStatus.open()
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 460)
        spacing: 18

        Image {
            source: "qrc:/icon/logo.svg"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: 44
            Layout.preferredWidth: 230
            fillMode: Image.PreserveAspectFit
            sourceSize.height: 96
            smooth: true
        }
        Label {
            text: App.hasOpenAccounts ? "Add or switch account"
                : (App.hasAccounts ? "Choose an account" : "Create your first account to begin")
            color: Theme.textDim
            Layout.alignment: Qt.AlignHCenter
        }

        Frame {
            Layout.fillWidth: true
            visible: App.hasAccounts
            background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
            ListView {
                id: list
                implicitHeight: Math.min(contentHeight, 320)
                width: parent.width
                clip: true
                model: App.accountList
                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 64
                    // An account on its way out is not one to open. Without this
                    // the row answered a press with nothing at all, because the
                    // session has to let go of its files before they can go and
                    // that takes as long as whatever it was doing.
                    readonly property bool goingAway: App.deletingId === model.accountId
                    enabled: !goingAway
                    opacity: goingAway ? 0.5 : 1
                    onClicked: {
                        // An encrypted account that is already open was unlocked
                        // once, and the session is still there: asking again
                        // would be asking for what is already held.
                        if (model.encrypted && !model.open) {
                            root.pendingId = model.accountId
                            passField.text = ""
                            passDialog.open()
                        } else {
                            App.openAccount(model.accountId, "")
                            root.closeIfSwitching()
                        }
                    }
                    // Fixed grid: the avatar and the text hug the left edge, the
                    // chip and the controls the right one, so rows line up
                    // whatever their content is.
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar {
                            fingerprint: model.fingerprint
                            size: 40
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            spacing: 2
                            Label {
                                // A locked account has no name to show: it is
                                // inside the database nobody has opened. What is
                                // on disk is a file named after nothing.
                                text: model.name.length > 0 ? model.name : "Locked account"
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // The second line is the fingerprint, or "locked"
                            // when the database has not been opened and there is
                            // none to show yet.
                            Label {
                                text: goingAway
                                    ? "Deleting…"
                                    : (model.fingerprint.length > 0
                                        ? model.fingerprint.substring(0, 12) + "…"
                                        : "locked")
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            // Closed while the passphrase is still needed, open
                            // once the account is: the mark says what a click
                            // will do, not merely that the file has a key.
                            text: !model.encrypted ? " " : (model.open ? "🔓" : "🔒")
                            color: Theme.textDim
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                        // Copy fingerprint and delete live in an overflow menu to
                        // keep the row clean at any width.
                        IconButton {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            iconName: "more"
                            onClicked: {
                                root.rowFingerprint = model.fingerprint
                                root.pendingDeleteId = model.accountId
                                root.pendingDeleteName = model.name
                                rowMenu.popup()
                            }
                        }
                    }
                }
            }
        }

        Button {
            Layout.fillWidth: true
            text: "Create account"
            hoverEnabled: true
            onClicked: root.StackView.view.push(wizardComponent)
            background: Rectangle { radius: 10; color: parent.down ? Qt.darker(Theme.accent, 1.2) : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: IconLabel { name: "plus"; color: Theme.accentText }
        }

    }

    Dialog {
        id: passDialog
        anchors.centerIn: parent
        modal: true
        title: "Unlock account"
        closePolicy: Popup.CloseOnEscape
        // Submitting is not closing. Dialog.accept() takes the prompt away the
        // moment the button is pressed, which is before anyone knows whether the
        // passphrase worked - and then there is nowhere to say that it did not.
        // This prompt closes when the account opens, or when the user cancels.
        function submit() {
            passError.text = ""
            App.openAccount(root.pendingId, passField.text)
        }
        onRejected: App.cancelUnlock()
        onOpened: passField.forceActiveFocus()
        background: DialogFrame { }
        header: Label { text: "Unlock account"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { acceptText: "Unlock"; onAccepted: passDialog.submit(); onRejected: passDialog.reject() }
        contentItem: ColumnLayout {
            spacing: 6
            TextField {
                id: passField
                echoMode: TextInput.Password
                placeholderText: "Passphrase"
                color: Theme.text
                placeholderTextColor: Theme.textDim
                Layout.preferredWidth: 280
                onAccepted: passDialog.submit()
                background: Rectangle { radius: 8; color: Theme.surface; border.color: passField.activeFocus ? Theme.accent : Theme.border }
            }
            // The reason a passphrase did not open the account belongs here.
            Label {
                id: passError
                visible: text.length > 0
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.preferredWidth: 280
            }
        }
    }

    Connections {
        target: App
        function onUnlockFailed(error) {
            passError.text = error
            passField.text = ""
            passField.forceActiveFocus()
            if (!passDialog.visible) {
                passDialog.open()
            }
        }
        // The account opened: the prompt has done its job and this screen with it.
        function onAccountUnlocked(id) {
            passError.text = ""
            passDialog.close()
            root.closeIfSwitching()
        }
    }

    AccountDeleteConfirmDialog {
        id: deleteDialog
        onLocalOnlyRequested: (id) => App.forgetAccountLocally(id)
        onConfirmed: (id) => App.deleteAccount(id)
    }

    // Per-row actions on a narrow window (the row fields are stashed on open).
    ContextMenu {
        id: rowMenu
        ContextMenuItem {
            text: "Copy fingerprint"
            onTriggered: root.copyFingerprint(root.rowFingerprint)
        }
        ContextMenuItem {
            text: "Delete account"
            danger: true
            onTriggered: deleteDialog.show(root.pendingDeleteId, root.pendingDeleteName)
        }
    }
}
