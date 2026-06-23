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
            Layout.preferredHeight: 48
            Layout.preferredWidth: 190
            fillMode: Image.PreserveAspectFit
            sourceSize.height: 96
            smooth: true
        }
        Label {
            text: App.hasOpenAccounts ? "Add or switch account"
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
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar { fingerprint: model.fingerprint; size: 40 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: model.name; color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.Medium }
                            Label {
                                text: (model.fingerprint.substring(0, 12) + "…")
                                    + (model.connected ? "" : "  · not connected")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                        }
                        Label { text: model.encrypted ? "🔒" : ""; color: Theme.textDim }
                        // Copy fingerprint and delete live in an overflow menu to
                        // keep the row clean at any width.
                        IconButton {
                            text: "⋮"
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
        onAccepted: App.openProfile(root.pendingId, passField.text)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: "Unlock profile"; color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        footer: DialogButtons { acceptText: "Unlock"; onAccepted: passDialog.accept(); onRejected: passDialog.reject() }
        contentItem: TextField {
            id: passField
            echoMode: TextInput.Password
            placeholderText: "Passphrase"
            color: Theme.text
            placeholderTextColor: Theme.textDim
            implicitWidth: 280
            onAccepted: passDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: passField.activeFocus ? Theme.accent : Theme.border }
        }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        modal: true
        width: 360
        title: "Delete profile"
        footer: DialogButtons { acceptText: "Delete"; danger: true; onAccepted: deleteDialog.accept(); onRejected: deleteDialog.reject() }
        onAccepted: App.deleteProfile(root.pendingDeleteId)
        // Destructive: brightest-neon outline, dark surface, light text.
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neonBright; border.width: 2 }
        header: Label { text: "Delete profile"; color: Theme.neonBright; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14 }
        contentItem: Label {
            text: "Permanently delete \"" + root.pendingDeleteName + "\" and all its "
                + "messages from this device? This cannot be undone."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    // Per-row actions on a narrow window (the row fields are stashed on open).
    Menu {
        id: rowMenu
        MenuItem {
            text: "Copy fingerprint"
            onTriggered: root.copyFingerprint(root.rowFingerprint)
        }
        MenuItem {
            text: "Delete profile"
            onTriggered: deleteDialog.open()
        }
    }
}
