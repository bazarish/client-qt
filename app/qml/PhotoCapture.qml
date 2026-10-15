// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Bazarish

// Taking a photograph to send. The shot is looked at before it goes anywhere: a
// frame nobody confirmed is not a message, and the shutter is the only place a
// camera is held open.
Popup {
    id: root

    readonly property int kEdge: 24
    modal: true
    // On a phone the window is the screen, so the viewfinder stands as the camera does.
    width: App.mobile && parent ? parent.width - root.kEdge
                               : Math.min(420, parent ? parent.width - root.kEdge : 420)
    height: App.mobile && parent ? parent.height - root.kEdge : implicitHeight
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogFrame { }

    signal confirmed(var shot)

    property string shotSource: ""
    property string errorText: ""
    readonly property bool taken: root.shotSource.length > 0

    CameraGate { id: cameraGate }
    Loader { id: cameras; active: cameraGate.granted; sourceComponent: MediaDevices { } }
    readonly property bool haveCamera: cameras.item !== null && cameras.item.videoInputs.length > 0

    onOpened: { root.shotSource = ""; root.errorText = ""; cameraGate.ask() }
    onClosed: { root.shotSource = ""; root.errorText = "" }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label {
                text: qsTr("Photo")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: App.mobile
            Layout.margins: 14
            Layout.preferredHeight: App.mobile ? -1 : 300
            radius: Theme.radiusSmall
            color: Theme.deep
            border.color: Theme.border

            Loader {
                id: live
                anchors.fill: parent
                anchors.margins: 1
                active: root.visible && root.haveCamera
                sourceComponent: VideoOutput {
                    id: preview
                    readonly property bool ready: shutter.readyForCapture
                    readonly property var heldShot: held
                    CaptureSession {
                        camera: Camera {
                            active: !root.taken
                            onErrorOccurred: function(error, errorString) {
                                root.errorText = errorString
                            }
                        }
                        imageCapture: ImageCapture { id: shutter }
                        videoOutput: preview
                    }
                    PhotoShot {
                        id: held
                        capture: shutter
                        onShotChanged: root.shotSource = held.source
                        onFailed: function(reason) { root.errorText = reason }
                    }
                }
            }

            Image {
                anchors.fill: parent
                anchors.margins: 1
                visible: root.taken
                fillMode: Image.PreserveAspectFit
                source: root.shotSource
            }

            Label {
                anchors.centerIn: parent
                visible: cameraGate.refused || (cameraGate.granted && !root.haveCamera)
                text: cameraGate.refused ? qsTr("No access to the camera.")
                                         : qsTr("No camera on this machine.")
                color: Theme.textDim
            }
        }

        Label {
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8

            MenuButton {
                iconName: "camera"
                text: root.taken ? qsTr("Take again") : qsTr("Take photo")
                enabled: live.item !== null && (root.taken || live.item.ready)
                onClicked: {
                    root.errorText = ""
                    if (root.taken) {
                        live.item.heldShot.discard()
                    } else {
                        live.item.heldShot.take()
                    }
                }
            }
            Item { Layout.fillWidth: true }
            MenuButton {
                iconName: "send"
                text: qsTr("Send")
                enabled: root.taken
                onClicked: {
                    root.confirmed(live.item.heldShot)
                    root.close()
                }
            }
        }
    }
}
