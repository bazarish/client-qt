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
    property string e2eId
    property string name
    property var session: null

    function show(url, e2eId, name) {
        root.source = url
        root.e2eId = e2eId
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
        onAccepted: root.session.savePictureAs(root.e2eId, selectedFile)
    }

    contentItem: Item {
        Image {
            id: picture
            anchors.fill: parent
            anchors.margins: 16
            source: root.source
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            asynchronous: true
            // Decoded at the size it is drawn at, so a large picture is not a
            // large allocation.
            sourceSize.width: root.width
            sourceSize.height: root.height
        }

        // A plain Item does not accept mouse events, so a press over it was still
        // delivered to the pointer handlers of the bubbles underneath - which is
        // how clicking "through" the picture opened another one. A MouseArea
        // accepts the press as an item, and delivery stops here.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            hoverEnabled: true
            preventStealing: true
            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton) {
                    pictureMenu.popup()
                } else {
                    root.close()
                }
            }
            onWheel: function(wheel) { wheel.accepted = true }
        }

        ContextMenu {
            id: pictureMenu
            ContextMenuItem {
                text: qsTr("Copy")
                onTriggered: root.session.copyPicture(root.e2eId)
            }
            ContextMenuItem {
                text: qsTr("Save as")
                onTriggered: {
                    saveDialog.currentFile
                        = root.session.defaultPictureSaveUrl(root.e2eId, root.name)
                    saveDialog.open()
                }
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            text: qsTr("Right-click for copy and save")
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }
    }
}
