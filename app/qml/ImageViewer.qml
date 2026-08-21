// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Bazarish

// A picture over the whole window. It takes the window, not the column it was
// opened from: a wide picture in a narrow chat pane was the reason to open it in
// the first place. Nothing under it can be clicked while it is up.
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

    // The window's overlay, so the size below is the window's size and not the
    // size of whatever opened this.
    parent: Overlay.overlay
    modal: true
    dim: true
    padding: 0
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    // Only Escape: a press anywhere is handled inside, so it never reaches the
    // chat behind.
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.92)
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
            anchors.margins: 16
            source: root.source
            fillMode: Image.PreserveAspectFit
            // Never upscale past the picture's own pixels: a small picture blown
            // up to a 4K window is mush.
            readonly property bool fits: implicitWidth <= width && implicitHeight <= height
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            asynchronous: true
            // Decoded at the size it is drawn at, so a large picture is not a
            // large allocation.
            sourceSize.width: root.width
            sourceSize.height: root.height
        }

        // A press anywhere closes it, and is consumed here rather than reaching
        // what is underneath.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: root.close()
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: pictureMenu.popup()
        }
        // Wheel events stop here too: a scroll over a picture must not scroll the
        // conversation behind it.
        WheelHandler { onWheel: function(event) { event.accepted = true } }

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

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            text: "Right-click for copy and save - click anywhere to close"
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }
    }
}
