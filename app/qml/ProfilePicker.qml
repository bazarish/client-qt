import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "profilePicker"

    Component { id: wizardComponent; CreateProfileWizard {} }

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

    // Off-screen helper used to put a fingerprint on the system clipboard.
    TextEdit { id: clip; visible: false }
    function copyFingerprint(fp) {
        clip.text = fp
        clip.selectAll()
        clip.copy()
        clip.deselect()
        if (typeof window !== "undefined") window.showToast("Fingerprint copied")
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
            text: App.hasOpenAccounts ? "Add or switch profile"
                : (App.hasProfiles ? "Choose a profile" : "Create your first profile to begin")
            color: Theme.textDim
            Layout.alignment: Qt.AlignHCenter
        }

        Frame {
            Layout.fillWidth: true
            visible: App.hasProfiles
            background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
            ListView {
                id: list
                implicitHeight: Math.min(contentHeight, 320)
                width: parent.width
                clip: true
                model: App.profiles
                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 64
                    onClicked: {
                        if (model.encrypted) {
                            root.pendingId = model.profileId
                            passField.text = ""
                            passDialog.open()
                        } else {
                            App.openProfile(model.profileId, "")
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
                                text: model.name
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // The second line is the fingerprint alone; whether a
                            // server is configured is the chip on the right, so
                            // every row is built the same way whatever its state.
                            Label {
                                text: model.fingerprint.length > 0
                                    ? model.fingerprint.substring(0, 12) + "…"
                                    : "locked"
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                Layout.fillWidth: true
                            }
                        }
                        // One chip per row, always: a profile with no server says
                        // so instead of leaving a hole where the others have text.
                        StatusChip {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            state: model.connected ? "configured" : "not configured"
                        }
                        Label {
                            text: model.encrypted ? "🔒" : " "
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
                                root.pendingDeleteId = model.profileId
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
            text: "Create profile"
            hoverEnabled: true
            onClicked: root.StackView.view.push(wizardComponent)
            background: Rectangle { radius: 10; color: parent.down ? Qt.darker(Theme.accent, 1.2) : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }

    }

    Dialog {
        id: passDialog
        anchors.centerIn: parent
        modal: true
        title: "Unlock profile"
        closePolicy: Popup.CloseOnEscape
        onAccepted: { App.openProfile(root.pendingId, passField.text); root.closeIfSwitching() }
        onRejected: App.cancelUnlock()
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Unlock profile"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { acceptText: "Unlock"; onAccepted: passDialog.accept(); onRejected: passDialog.reject() }
        contentItem: ColumnLayout {
            spacing: 6
            TextField {
                id: passField
                echoMode: TextInput.Password
                placeholderText: "Passphrase"
                color: Theme.text
                placeholderTextColor: Theme.textDim
                Layout.preferredWidth: 280
                onAccepted: passDialog.accept()
                background: Rectangle { radius: 8; color: Theme.surface; border.color: passField.activeFocus ? Theme.accent : Theme.border }
            }
            // The reason a passphrase did not open the profile belongs here.
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
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        modal: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        title: "Delete profile"
        footer: DialogButtons { acceptText: "Delete"; danger: true; onAccepted: deleteDialog.accept(); onRejected: deleteDialog.reject() }
        onAccepted: App.deleteProfile(root.pendingDeleteId)
        // Destructive: brightest-neon outline, dark surface, light text.
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }
        header: Label { text: "Delete profile"; color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        contentItem: Label {
            text: "Permanently delete \"" + root.pendingDeleteName + "\" and all its "
                + "messages from this device? This cannot be undone."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    // Per-row actions on a narrow window (the row fields are stashed on open).
    ContextMenu {
        id: rowMenu
        ContextMenuItem {
            text: "Copy fingerprint"
            onTriggered: root.copyFingerprint(root.rowFingerprint)
        }
        ContextMenuItem {
            text: "Delete profile"
            danger: true
            onTriggered: deleteDialog.open()
        }
    }
}
