import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "profilePicker"

    // On a narrow window the per-row copy/delete buttons are hidden to keep the
    // row readable (deletion stays available from Settings).
    property bool compact: width < 430

    Component { id: wizardComponent; CreateProfileWizard {} }

    property string pendingId: ""
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""

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

        Label {
            text: "Bazarish"
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
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
                        IconButton {
                            text: "📋"
                            visible: !root.compact
                            onClicked: root.copyFingerprint(model.fingerprint)
                        }
                        IconButton {
                            text: "🗑"
                            visible: !root.compact
                            onClicked: {
                                root.pendingDeleteId = model.profileId
                                root.pendingDeleteName = model.name
                                deleteDialog.open()
                            }
                        }
                    }
                }
            }
        }

        Button {
            Layout.fillWidth: true
            text: "Create profile"
            onClicked: root.StackView.view.push(wizardComponent)
            background: Rectangle { radius: 10; color: parent.down ? Qt.darker(Theme.accent, 1.1) : Theme.accent }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }

        // When other accounts are already open, this picker was opened to add
        // one; let the user return to the running session instead.
        Button {
            Layout.fillWidth: true
            visible: App.hasOpenAccounts
            text: "Back"
            onClicked: root.StackView.view.pop()
            background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
            contentItem: Label { text: parent.text; color: Theme.text; horizontalAlignment: Text.AlignHCenter }
        }
    }

    Dialog {
        id: passDialog
        anchors.centerIn: parent
        modal: true
        title: "Unlock profile"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: App.openProfile(root.pendingId, passField.text)
        contentItem: TextField {
            id: passField
            echoMode: TextInput.Password
            placeholderText: "Passphrase"
            implicitWidth: 280
        }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        modal: true
        width: 360
        title: "Delete profile"
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: App.deleteProfile(root.pendingDeleteId)
        contentItem: Label {
            text: "Permanently delete \"" + root.pendingDeleteName + "\" and all its "
                + "messages from this device? This cannot be undone."
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
}
