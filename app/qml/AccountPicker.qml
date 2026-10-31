// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "accountPicker"

    Component { id: wizardComponent; CreateAccountWizard {} }

    function openPairing(link) {
        root.StackView.view.push(wizardComponent).pairLink = link
    }

    property string pendingId: ""
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""
    property string rowFingerprint: ""

    function closeIfSwitching() {
        const view = root.StackView.view
        if (App.hasOpenAccounts && view && view.depth > 1) {
            view.pop()
        }
    }

    function copyFingerprint(fp) {
        App.copyText(fp)
        window.showToast(qsTr("Fingerprint copied"))
    }

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
        width: Math.min(parent.width - 2 * Theme.spacing, 460)
        spacing: 18

        Image {
            source: Theme.light ? "qrc:/icon/logo-onlight.svg" : "qrc:/icon/logo.svg"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: 44
            Layout.preferredWidth: 230
            fillMode: Image.PreserveAspectFit
            sourceSize.height: 96
            smooth: true
        }
        Label {
            text: App.hasOpenAccounts ? qsTr("Add or switch account")
                : (App.hasAccounts ? qsTr("Choose an account")
                                   : qsTr("Create your first account to begin"))
            color: Theme.textDim
            Layout.alignment: Qt.AlignHCenter
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        Frame {
            Layout.fillWidth: true
            visible: App.hasAccounts
            background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
            ListView {
                implicitHeight: Math.min(contentHeight, 320)
                width: parent.width
                clip: true
                model: App.accountList
                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 64
                    readonly property bool goingAway: App.deletingId === model.accountId
                    enabled: !goingAway
                    opacity: goingAway ? 0.5 : 1
                    onClicked: {
                        if (model.encrypted && !model.open) {
                            root.pendingId = model.accountId
                            passField.text = ""
                            passDialog.open()
                        } else {
                            App.openAccount(model.accountId, "")
                            root.closeIfSwitching()
                        }
                    }
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
                                text: model.name.length > 0 ? model.name : qsTr("Locked account")
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: goingAway
                                    ? qsTr("Deleting…")
                                    : (model.fingerprint.length > 0
                                        ? model.fingerprint.substring(0, 12) + "…"
                                        : qsTr("locked"))
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: !model.encrypted ? " " : (model.open ? "🔓" : "🔒")
                            color: Theme.textDim
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
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
            focusPolicy: Theme.pressFocus
            Layout.fillWidth: true
            text: qsTr("Create account")
            hoverEnabled: true
            onClicked: root.StackView.view.push(wizardComponent)
            background: Rectangle { radius: 10; color: parent.down ? Theme.accentDown : (parent.hovered ? Theme.accentHover : Theme.accent) }
            contentItem: IconLabel { name: "plus"; color: Theme.accentText }
        }

    }

    Dialog {
        id: passDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: parent
        modal: true
        focus: true
        title: qsTr("Unlock account")
        closePolicy: Popup.CloseOnEscape
        // Not Dialog.accept(): that closes the prompt before the passphrase is known to work.
        function submit() {
            passError.text = ""
            App.openAccount(root.pendingId, passField.text)
        }
        onRejected: App.cancelUnlock()
        onOpened: passField.forceActiveFocus()
        background: DialogFrame { }
        header: Label { text: qsTr("Unlock account"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { acceptText: qsTr("Unlock"); onAccepted: passDialog.submit(); onRejected: passDialog.reject() }
        contentItem: ColumnLayout {
            spacing: 6
            TextField {
                id: passField
                echoMode: TextInput.Password
                placeholderText: qsTr("Passphrase")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                Layout.preferredWidth: 280
                onAccepted: passDialog.submit()
                background: Rectangle { radius: 8; color: Theme.surface; border.color: passField.activeFocus ? Theme.accent : Theme.border }
            }
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

    ContextMenu {
        id: rowMenu
        ContextMenuItem {
            text: qsTr("Copy fingerprint")
            onTriggered: root.copyFingerprint(root.rowFingerprint)
        }
        ContextMenuItem {
            text: qsTr("Delete account")
            danger: true
            onTriggered: deleteDialog.show(root.pendingDeleteId, root.pendingDeleteName)
        }
    }
}
