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
    signal cameraFailed(string reason)

    readonly property int kFrame: 8
    readonly property int kAboveDialogs: 2000
    readonly property int kCloseSize: 44
    readonly property real kScrim: 0.55
    readonly property int kPickerWidth: 320

    // Which of the machine's cameras is shown; -1 is the one chosen in settings, or the system's default.
    property int cameraAt: -1
    readonly property var inputs: cameras.item !== null ? cameras.item.videoInputs : []
    readonly property bool canSwitchCamera: sheet.inputs.length > 1
    function idOf(device) {
        return String.fromCharCode.apply(null, new Uint8Array(device.id))
    }
    readonly property int chosenAt: {
        for (let i = 0; i < sheet.inputs.length; ++i) {
            if (sheet.idOf(sheet.inputs[i]) === App.camera) {
                return i
            }
        }
        return -1
    }
    readonly property int shownAt: {
        if (sheet.cameraAt >= 0) {
            return sheet.cameraAt % sheet.inputs.length
        }
        if (sheet.chosenAt >= 0) {
            return sheet.chosenAt
        }
        for (let i = 0; i < sheet.inputs.length; ++i) {
            if (sheet.inputs[i].isDefault) {
                return i
            }
        }
        return sheet.inputs.length > 0 ? 0 : -1
    }
    function switchCamera() {
        sheet.cameraAt = (sheet.shownAt + 1) % sheet.inputs.length
    }
    function choose(index) {
        App.camera = sheet.idOf(sheet.inputs[index])
        sheet.cameraAt = -1
        picker.close()
    }

    parent: Overlay.overlay
    x: 0
    y: 0
    z: sheet.kAboveDialogs
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: sheet.kFrame
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.bg }

    onOpened: {
        Qt.inputMethod.hide()
        cameraGate.ask()
        if (sheet.contentItem.missing) {
            picker.open()
        }
    }
    onClosed: picker.close()

    contentItem: Item {
        clip: true

        CameraGate { id: cameraGate }
        Loader { id: cameras; active: cameraGate.granted; sourceComponent: MediaDevices { } }
        readonly property bool haveCamera: sheet.inputs.length > 0
        readonly property bool missing: cameraGate.granted && !haveCamera
        onMissingChanged: if (missing && sheet.opened) { picker.open() }

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
                fillMode: VideoOutput.PreserveAspectCrop
                CaptureSession {
                    camera: Camera {
                        active: sheet.cameraActive
                        cameraDevice: sheet.inputs[sheet.shownAt]
                        onErrorOccurred: function(error, errorString) {
                            sheet.cameraFailed(errorString)
                        }
                    }
                    videoOutput: view
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: cameraGate.refused || (cameraGate.granted && !parent.haveCamera)
            text: cameraGate.refused ? qsTr("No access to the camera.")
                : (App.cameraSupported ? qsTr("No camera on this machine.")
                                       : qsTr("This build of Bazarish cannot use a camera."))
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

        Button {
            id: gear
            focusPolicy: Theme.pressFocus
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: sheet.kFrame
            anchors.rightMargin: 2 * sheet.kFrame + sheet.kCloseSize
            width: sheet.kCloseSize
            height: sheet.kCloseSize
            padding: 0
            Accessible.name: qsTr("Camera")
            background: Rectangle {
                radius: width / 2
                color: Qt.rgba(0, 0, 0, sheet.kScrim)
            }
            contentItem: Item {
                Icon { anchors.centerIn: parent; name: "gear"; color: "white" }
            }
            onClicked: picker.opened ? picker.close() : picker.open()
        }

        Popup {
            id: picker
            x: parent.width - width - sheet.kFrame
            y: gear.y + gear.height + sheet.kFrame
            width: Math.min(sheet.kPickerWidth, parent.width - 2 * sheet.kFrame)
            padding: Theme.spacing
            modal: true
            dim: false
            focus: true
            background: DialogFrame { }
            contentItem: Column {
                spacing: Theme.spacing / 2
                Label {
                    text: qsTr("Camera")
                    color: Theme.green
                    font.weight: Font.DemiBold
                }
                Label {
                    visible: sheet.inputs.length === 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    text: App.cameraSupported ? qsTr("No camera on this machine.")
                                              : qsTr("This build of Bazarish cannot use a camera.")
                }
                Repeater {
                    model: sheet.inputs
                    ItemDelegate {
                        id: cameraRow
                        required property var modelData
                        required property int index
                        width: parent.width
                        focusPolicy: Theme.pressFocus
                        contentItem: Row {
                            spacing: Theme.spacing
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "check"
                                color: Theme.green
                                opacity: cameraRow.index === sheet.shownAt ? 1 : 0
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - parent.spacing - parent.children[0].width
                                text: cameraRow.modelData.description
                                color: Theme.text
                                elide: Text.ElideRight
                            }
                        }
                        onClicked: sheet.choose(cameraRow.index)
                    }
                }
            }
        }
    }
}
