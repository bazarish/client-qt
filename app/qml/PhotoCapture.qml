// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Bazarish

// Taking a photograph to send. The shot is looked at before it goes anywhere: a
// frame nobody confirmed is not a message, and the shutter is the only place a
// camera is held open.
CameraSheet {
    id: root

    signal confirmed(var shot)

    property string shotSource: ""
    property string errorText: ""
    readonly property bool taken: root.shotSource.length > 0
    readonly property int kShutter: 68
    readonly property int kShutterRing: 4
    readonly property int kBarMargin: 20

    cameraActive: !root.taken
    onOpened: { root.shotSource = ""; root.errorText = "" }
    onClosed: { root.shotSource = ""; root.errorText = "" }
    onCameraFailed: function(reason) { root.errorText = reason }

    PhotoShot {
        id: held
        capture: root.imageCapture
        onShotChanged: root.shotSource = held.source
        onFailed: function(reason) { root.errorText = reason }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.taken
        color: Theme.deep
        Image {
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            source: root.shotSource
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.kBarMargin
        spacing: 12

        Label {
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.danger
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        Button {
            id: shutter
            focusPolicy: Theme.pressFocus
            visible: !root.taken
            enabled: root.imageCapture !== null && root.imageCapture.readyForCapture
            Accessible.name: qsTr("Take photo")
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.kShutter
            Layout.preferredHeight: root.kShutter
            padding: 0
            background: Rectangle {
                radius: width / 2
                color: shutter.down ? Theme.textDim : "white"
                opacity: shutter.enabled ? 1 : 0.4
                border.width: root.kShutterRing
                border.color: Theme.bg
            }
            onClicked: {
                root.errorText = ""
                held.take()
            }
        }

        RowLayout {
            visible: root.taken
            Layout.fillWidth: true
            MenuButton {
                iconName: "camera"
                text: qsTr("Take again")
                onClicked: {
                    root.errorText = ""
                    held.discard()
                }
            }
            Item { Layout.fillWidth: true }
            MenuButton {
                iconName: "send"
                text: qsTr("Send")
                onClicked: {
                    root.confirmed(held)
                    root.close()
                }
            }
        }
    }
}
