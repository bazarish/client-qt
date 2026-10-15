// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Bazarish

ColumnLayout {
    id: box
    property bool scanning: false
    signal decoded(string text)
    signal failed(string reason)

    readonly property int kViewHeight: 240
    visible: box.scanning
    spacing: 0
    Layout.fillWidth: true

    // The keyboard would take the height the camera is shown in.
    onScanningChanged: if (box.scanning) {
        Qt.inputMethod.hide()
        cameraGate.ask()
    }

    CameraGate { id: cameraGate }
    Label {
        visible: cameraGate.refused
        text: qsTr("No access to the camera.")
        color: Theme.textDim
        Layout.fillWidth: true
    }
    Loader {
        active: box.scanning && cameraGate.granted
        visible: active
        Layout.fillWidth: true
        Layout.preferredHeight: box.kViewHeight
        sourceComponent: VideoOutput {
            id: preview
            MediaDevices { id: cameras }
            CaptureSession {
                camera: Camera {
                    active: cameras.videoInputs.length > 0
                    onErrorOccurred: function(error, errorString) {
                        box.scanning = false
                        box.failed(errorString)
                    }
                }
                videoOutput: preview
            }
            QrScanner {
                sink: preview.videoSink
                onDecoded: function(text) {
                    box.scanning = false
                    box.decoded(text)
                }
            }
            Label {
                anchors.centerIn: parent
                visible: cameras.videoInputs.length === 0
                text: qsTr("No camera on this machine.")
                color: Theme.textDim
            }
        }
    }
}
