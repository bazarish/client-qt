import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

// Saving a received attachment: an editable file name (pre-filled from the
// message), a folder chooser, and live download progress / error feedback -
// styled to the app's dialog language (Settings / New Chat).
Dialog {
    id: root
    property var session: null
    property string attRef: ""
    property string attKey: ""
    property string defaultName: ""
    property real fileSize: 0
    // Correlates the worker's downloadFinished signal with this dialog.
    property var token: 0

    property url folder: ""
    property bool saving: false
    property string errorText: ""

    function humanSize(n) {
        if (!n || n <= 0) {
            return ""
        }
        const u = ["B", "KB", "MB", "GB"]
        var v = n
        var i = 0
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return (i === 0 ? v : v.toFixed(1)) + " " + u[i]
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: 400
    padding: 0
    // While downloading, do not let a click-outside / Escape dismiss it.
    closePolicy: root.saving ? Popup.NoAutoClose
                             : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    onOpened: {
        var d = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        if (("" + d).length === 0) {
            d = StandardPaths.writableLocation(StandardPaths.HomeLocation)
        }
        root.folder = d
        nameField.text = root.defaultName
        root.saving = false
        root.errorText = ""
    }

    function startSave() {
        if (root.saving || !root.session) {
            return
        }
        if (nameField.text.trim().length === 0 || ("" + root.folder).length === 0) {
            return
        }
        root.errorText = ""
        root.saving = true
        root.session.saveAttachmentToFolder(
            root.attRef, root.attKey, root.folder, nameField.text.trim(), root.token)
    }

    // The download outcome for this attachment (filtered by token).
    Connections {
        target: root.session
        enabled: root.saving
        function onDownloadFinished(token, ok, error) {
            if (token !== root.token) {
                return
            }
            root.saving = false
            if (ok) {
                root.close()
            } else {
                root.errorText = (error && error.length > 0) ? error : "Download failed."
            }
        }
    }

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
    header: Label {
        text: "Save file"
        color: Theme.green
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: root.saving ? "Saving…" : "Save"
        onAccepted: root.startSave()
        onRejected: root.close()
    }

    // Folder browser (opened from the Browse button below).
    FolderDialog {
        id: folderPicker
        title: "Choose a folder"
        onAccepted: root.folder = selectedFolder
    }

    contentItem: ColumnLayout {
        spacing: 12

        // Editable file name, pre-filled from the message.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            spacing: 4
            Label { text: "File name:"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            TextField {
                id: nameField
                Layout.fillWidth: true
                enabled: !root.saving
                color: Theme.text
                selectByMouse: true
                onAccepted: root.startSave()
                background: Rectangle {
                    radius: 8
                    color: Theme.surface
                    border.color: nameField.activeFocus ? Theme.accent : Theme.border
                }
            }
        }

        // Destination folder + Browse.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 4
            Label { text: "Folder:"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: decodeURIComponent(("" + root.folder).replace("file://", ""))
                    color: Theme.text
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }
                MenuButton {
                    text: "Browse…"
                    enabled: !root.saving
                    onClicked: { folderPicker.currentFolder = root.folder; folderPicker.open() }
                }
            }
        }

        // Live status: an indeterminate bar while downloading (the I2P transport
        // does not expose byte progress), then an inline error on failure.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 12
            spacing: 6
            ProgressBar {
                visible: root.saving
                Layout.fillWidth: true
                indeterminate: true
            }
            Label {
                visible: root.saving
                Layout.fillWidth: true
                text: "Downloading"
                    + (root.fileSize > 0 ? " " + root.humanSize(root.fileSize) : "")
                    + " over I2P…"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            Label {
                visible: root.errorText.length > 0
                Layout.fillWidth: true
                text: root.errorText
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }
}
