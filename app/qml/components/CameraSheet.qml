// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtMultimedia
import Bazarish

// The camera takes the whole window: a preview inside a dialog is too small to aim with.
Popup {
    id: sheet

    default property alias controls: controlLayer.data
    property bool cameraActive: true
    readonly property VideoOutput preview: live.item
    readonly property ImageCapture imageCapture: live.item ? live.item.shutter : null
    signal cameraFailed(string reason)

    readonly property int kFrame: 8
    readonly property int kAboveDialogs: 2000
    readonly property int kCloseSize: 44
    readonly property real kScrim: 0.55

    parent: Overlay.overlay
    x: 0
    y: 0
    z: sheet.kAboveDialogs
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: sheet.kFrame
    modal: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.bg }

    onOpened: {
        Qt.inputMethod.hide()
        cameraGate.ask()
    }

    contentItem: Item {
        clip: true

        CameraGate { id: cameraGate }
        Loader { id: cameras; active: cameraGate.granted; sourceComponent: MediaDevices { } }
        readonly property bool haveCamera: cameras.item !== null
            && cameras.item.videoInputs.length > 0

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: Theme.deep
        }
        Loader {
            id: live
            anchors.fill: parent
            active: sheet.visible && parent.haveCamera
            sourceComponent: VideoOutput {
                id: view
                readonly property ImageCapture shutter: still
                fillMode: VideoOutput.PreserveAspectCrop
                CaptureSession {
                    camera: Camera {
                        active: sheet.cameraActive
                        onErrorOccurred: function(error, errorString) {
                            sheet.cameraFailed(errorString)
                        }
                    }
                    imageCapture: ImageCapture { id: still }
                    videoOutput: view
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: cameraGate.refused || (cameraGate.granted && !parent.haveCamera)
            text: cameraGate.refused ? qsTr("No access to the camera.")
                                     : qsTr("No camera on this machine.")
            color: Theme.textDim
        }

        Item {
            id: controlLayer
            anchors.fill: parent
        }

        Button {
            focusPolicy: Theme.pressFocus
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: sheet.kFrame
            width: sheet.kCloseSize
            height: sheet.kCloseSize
            padding: 0
            Accessible.name: qsTr("Close")
            background: Rectangle {
                radius: width / 2
                color: Qt.rgba(0, 0, 0, sheet.kScrim)
            }
            contentItem: Item {
                Icon { anchors.centerIn: parent; name: "close"; color: "white" }
            }
            onClicked: sheet.close()
        }
    }
}
