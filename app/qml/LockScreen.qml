// Bazarish project (c) 2026
import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Bazarish

Popup {
    id: root
    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    z: Theme.lockLayer
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    visible: App.uiLocked || pins.busy
    background: Rectangle { color: Theme.bg }

    readonly property int kTickMs: 250
    readonly property int kCameraSettleMs: 700
    property double now: Date.now()
    readonly property bool waiting: App.uiWaitUntil > root.now
    property bool fingerprintOffered: false
    property bool cameIn: true
    property bool byHand: false

    Timer {
        running: root.visible && root.waiting
        interval: root.kTickMs
        repeat: true
        triggeredOnStart: true
        onTriggered: root.now = Date.now()
    }

    function showUp() {
        if (!root.opened || (App.mobile && Qt.application.state !== Qt.ApplicationActive)) {
            return
        }
        pins.focusInput()
        if (root.cameIn) {
            root.cameIn = false
            if (root.fingerprintOffered) {
                App.askFingerprint()
            }
        }
    }
    onOpened: {
        root.now = Date.now()
        root.fingerprintOffered = App.mobile && App.uiLockFingerprint && App.fingerprintReady()
        if (root.byHand) {
            root.byHand = false
            root.cameIn = false
        }
        root.showUp()
    }
    onClosed: {
        root.cameIn = false
        shooter.active = false
    }
    Connections {
        target: Qt.application
        function onStateChanged() {
            const state = Qt.application.state
            if (state === Qt.ApplicationSuspended || state === Qt.ApplicationHidden) {
                root.cameIn = true
            } else if (state === Qt.ApplicationActive) {
                root.showUp()
            }
        }
    }
    Connections {
        target: App
        function onUiUnlockAnswered(unlocked) { pins.answered(unlocked) }
        function onFingerprintAnswered(ok) {
            if (!ok) {
                pins.focusInput()
            }
        }
        function onUiLockRequestedHere() { root.byHand = true }
        function onIntruderPhotoWanted() { root.shoot() }
    }

    MediaDevices { id: devices }
    CameraPermission { id: cameraGate }
    readonly property var frontCamera: {
        for (const device of devices.videoInputs) {
            if (device.position === CameraDevice.FrontFace) {
                return device
            }
        }
        return null
    }
    function stopCamera() {
        Qt.callLater(() => { shooter.active = false })
    }
    function shoot() {
        if (App.mobile && root.frontCamera !== null
                && cameraGate.status === Qt.PermissionStatus.Granted) {
            shooter.active = true
        }
    }
    Loader {
        id: shooter
        active: false
        width: 1
        height: 1
        opacity: 0
        sourceComponent: Item {
            VideoOutput {
                id: view
                anchors.fill: parent
                CaptureSession {
                    camera: Camera {
                        id: camera
                        cameraDevice: root.frontCamera
                        onErrorOccurred: root.stopCamera()
                        Component.onCompleted: camera.active = true
                    }
                    videoOutput: view
                }
            }
            PhotoShot {
                id: shot
                videoSink: view.videoSink
                onReadyChanged: if (shot.ready) { settle.start() }
                onShotChanged: {
                    if (shot.source.length > 0) {
                        App.keepIntruderPhoto(shot)
                        shot.discard()
                        root.stopCamera()
                    }
                }
                onFailed: root.stopCamera()
            }
            Timer { id: settle; interval: root.kCameraSettleMs; onTriggered: shot.take() }
        }
    }

    contentItem: Item {
        Item {
            id: room
            anchors.fill: parent
            anchors.bottomMargin: App.keyboardInset
        }
        ColumnLayout {
            id: column
            readonly property int kLogoHeight: 44
            readonly property bool roomy: room.height >= column.kLogoHeight + column.spacing
                + pins.implicitHeight + (root.fingerprintOffered ? column.spacing + fingerprint.implicitHeight : 0)
                + 2 * Theme.spacing
            x: (parent.width - width) / 2
            y: Math.min((room.height - height) / 2, room.height - height)
            width: Math.min(parent.width - 2 * Theme.spacing, 460)
            spacing: 28

            Image {
                visible: column.roomy
                source: Theme.light ? "qrc:/icon/logo-onlight.svg" : "qrc:/icon/logo.svg"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: column.kLogoHeight
                Layout.preferredWidth: 230
                fillMode: Image.PreserveAspectFit
                sourceSize.height: 96
                smooth: true
            }
            PinRow {
                id: pins
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                waiting: root.waiting
                actionName: qsTr("Unlock the app")
                onSubmitted: (pin) => App.unlockUi(pin)
            }
            IconButton {
                id: fingerprint
                visible: root.fingerprintOffered
                Layout.alignment: Qt.AlignHCenter
                iconName: "fingerprint"
                iconSize: 28
                implicitWidth: 52
                implicitHeight: 52
                Accessible.name: qsTr("Unlock by fingerprint")
                onClicked: App.askFingerprint()
            }
        }
    }
}
