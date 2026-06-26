import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Bazarish

// Choosing where to save a received attachment: an editable file name
// (pre-filled from the message) and a folder. On Save the dialog closes at once
// and the download runs in the background - its progress and any error show on
// the message bubble itself. Styled to the app's dialog language.
Dialog {
    id: root
    property var session: null
    property string attRef: ""
    property string attKey: ""
    property string defaultName: ""
    // Correlates the download progress/outcome back to the message bubble.
    property var token: 0

    property url folder: ""

    anchors.centerIn: Overlay.overlay
    modal: true
    width: 400
    padding: 0

    onOpened: {
        var d = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        if (("" + d).length === 0) {
            d = StandardPaths.writableLocation(StandardPaths.HomeLocation)
        }
        root.folder = d
        nameField.text = root.defaultName
    }

    function commit() {
        if (!root.session || nameField.text.trim().length === 0
                || ("" + root.folder).length === 0) {
            return
        }
        // Kick off the background download, then unblock the UI immediately.
        root.session.saveAttachmentToFolder(
            root.attRef, root.attKey, root.folder, nameField.text.trim(), root.token)
        root.close()
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
        acceptText: "Save"
        onAccepted: root.commit()
        onRejected: root.close()
    }

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
                color: Theme.text
                selectByMouse: true
                onAccepted: root.commit()
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
            Layout.bottomMargin: 12
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
                    onClicked: { folderPicker.currentFolder = root.folder; folderPicker.open() }
                }
            }
        }
    }
}
