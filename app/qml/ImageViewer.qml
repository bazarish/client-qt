// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Bazarish

// A picture at full size, over everything else. It shows what is already on this
// machine - a file whose own first bytes said it was a picture - and nothing it
// is given can reach a renderer any other way.
Popup {
    id: root
    property url source
    property string messageId
    property string name
    property var session: null

    function show(url, messageId, name) {
        root.source = url
        root.messageId = messageId
        root.name = name
        root.open()
    }

    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 48 : 800, picture.implicitWidth + 2)
    height: Math.min(parent ? parent.height - 48 : 600, picture.implicitHeight + 2)

    background: Rectangle {
        color: Theme.deep
        border.color: Theme.border
        radius: Theme.radius
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        onAccepted: root.session.savePictureAs(root.messageId, selectedFile)
    }

    contentItem: Item {
        Image {
            id: picture
            anchors.fill: parent
            anchors.margins: 1
            source: root.source
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            // A picture from a stranger is not allowed to become a huge
            // allocation: it is drawn no larger than the window it opens in.
            sourceSize.width: root.parent ? root.parent.width : 1920
            sourceSize.height: root.parent ? root.parent.height : 1080
        }
        // Right-click (or a long press) offers what can be done with the picture.
        // A button standing on top of it forever was one thing too many.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: pictureMenu.popup()
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onLongPressed: pictureMenu.popup()
        }

        ContextMenu {
            id: pictureMenu
            ContextMenuItem {
                text: "Copy"
                onTriggered: root.session.copyPicture(root.messageId)
            }
            ContextMenuItem {
                text: "Save as"
                onTriggered: {
                    saveDialog.currentFile
                        = root.session.defaultPictureSaveUrl(root.messageId, root.name)
                    saveDialog.open()
                }
            }
        }

        TapHandler { acceptedButtons: Qt.LeftButton; onTapped: root.close() }
    }
}
