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

    readonly property real kMaxZoom: 4
    readonly property real kDoubleTapZoom: 2.5
    // One notch of a mouse wheel; a wheel notch is 120 in angleDelta.
    readonly property real kWheelStep: 1.25
    readonly property int kWheelNotch: 120
    // Decoded no bigger than this: this client sends a picture at most 1600 px, a hostile one is capped.
    readonly property int kMaxDecodeEdge: 2048
    // Pulled this far while not zoomed, the picture turns to the one beside it in the chat.
    readonly property int kTurnPull: 80
    readonly property int kSettleMs: 180
    readonly property int kButtonSize: 44
    readonly property int kButtonMargin: 12
    readonly property real kButtonScrim: 0.55
    property real pull: 0

    function show(url, e2eId, name) {
        root.source = url
        root.e2eId = e2eId
        root.name = name
        view.fit()
        root.open()
    }
    // +1 is the later picture of the chat, -1 the earlier one.
    function turn(step) {
        const beside = root.session ? root.session.pictureBeside(root.e2eId, step) : null
        if (!beside || !beside.found) {
            return false
        }
        root.show("image://picture/" + beside.e2eId + "?r=" + Pictures.revision,
            beside.e2eId, beside.name)
        return true
    }

    // The window's overlay, so the size below is the window's size and not the
    // size of whatever opened this.
    parent: Overlay.overlay
    modal: true
    focus: true
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
        focus: true
        Keys.onLeftPressed: root.turn(-1)
        Keys.onRightPressed: root.turn(1)

        // Takes every press the picture leaves, so none reaches the chat behind.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            hoverEnabled: true
            onWheel: function(wheel) { wheel.accepted = true }
        }

        Flickable {
            id: view
            anchors.fill: parent
            anchors.margins: 16
            contentWidth: width
            contentHeight: height
            readonly property bool zoomed: contentWidth > width + 1
            interactive: zoomed
            boundsBehavior: Flickable.StopAtBounds
            function fit() {
                contentWidth = width
                contentHeight = height
                contentX = 0
                contentY = 0
            }
            onWidthChanged: fit()
            onHeightChanged: fit()
            function zoomTo(zoom, at) {
                const bounded = Math.max(1, Math.min(root.kMaxZoom, zoom))
                resizeContent(width * bounded, height * bounded, at)
                returnToBounds()
            }

            Image {
                id: picture
                width: view.contentWidth
                height: view.contentHeight
                transform: Translate { x: root.pull }
                source: root.source
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignHCenter
                verticalAlignment: Image.AlignVCenter
                asynchronous: true
                sourceSize.width: root.kMaxDecodeEdge
                sourceSize.height: root.kMaxDecodeEdge

                // The press stops here, before anything behind could see it; the gestures ride on it.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            pictureMenu.popup()
                        } else {
                            closing.start()
                        }
                    }
                    onDoubleClicked: function(mouse) {
                        closing.stop()
                        view.zoomTo(view.zoomed ? 1 : root.kDoubleTapZoom, Qt.point(mouse.x, mouse.y))
                    }
                    onPressAndHold: pictureMenu.popup()
                    onWheel: function(wheel) {
                        const notches = wheel.angleDelta.y / root.kWheelNotch
                        view.zoomTo(view.contentWidth / view.width * Math.pow(root.kWheelStep, notches),
                            Qt.point(wheel.x, wheel.y))
                    }

                    PinchHandler {
                        target: null
                        rotationAxis.enabled: false
                        property real fromZoom: 1
                        onActiveChanged: {
                            if (active) {
                                fromZoom = view.contentWidth / view.width
                            } else {
                                view.returnToBounds()
                            }
                        }
                        onActiveScaleChanged: {
                            const zoom = Math.max(1, Math.min(root.kMaxZoom, fromZoom * activeScale))
                            view.resizeContent(view.width * zoom, view.height * zoom, centroid.position)
                        }
                    }
                    DragHandler {
                        target: null
                        enabled: !view.zoomed
                        yAxis.enabled: false
                        onActiveTranslationChanged: root.pull = activeTranslation.x
                        onActiveChanged: {
                            if (active) {
                                settle.stop()
                                return
                            }
                            const step = root.pull <= -root.kTurnPull ? 1
                                : (root.pull >= root.kTurnPull ? -1 : 0)
                            if (step !== 0 && root.turn(step)) {
                                root.pull = 0
                            } else {
                                settle.start()
                            }
                        }
                    }
                }
            }
        }
        Button {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: root.kButtonMargin
            width: root.kButtonSize
            height: root.kButtonSize
            padding: 0
            focusPolicy: Theme.pressFocus
            Accessible.name: qsTr("Save to Downloads")
            background: Rectangle {
                radius: width / 2
                color: Qt.rgba(0, 0, 0, root.kButtonScrim)
            }
            contentItem: Item {
                Icon { anchors.centerIn: parent; name: "save"; color: "white" }
            }
            onClicked: root.session.savePictureToDownloads(root.e2eId, root.name)
        }
        // A tap waits out the double-tap interval: a second one zooms instead of closing.
        Timer {
            id: closing
            interval: Application.styleHints.mouseDoubleClickInterval
            onTriggered: root.close()
        }
        NumberAnimation {
            id: settle
            target: root
            property: "pull"
            to: 0
            duration: root.kSettleMs
            easing.type: Easing.OutCubic
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
    }
}
