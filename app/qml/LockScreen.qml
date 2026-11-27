// Bazarish project (c) 2026
import QtCore
import QtQuick
import QtQuick.Controls
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

    readonly property int kCameraSettleMs: 700
    property bool fingerprintOffered: false
    property bool cameIn: true
    property bool byHand: false
    property bool prompting: false
    function askFingerprint() {
        root.prompting = true
        App.askFingerprint()
    }

    function reclaimFocus() {
        const item = root.contentItem.Window.activeFocusItem
        if (root.opened && App.inFront && item && !pins.keyboardDismissed
                && (item === root.contentItem || item === root.contentItem.parent)) {
            pins.focusInput()
        }
    }
    function showUp() {
        if (!root.opened || (App.mobile && !App.inFront)) {
            return
        }
        pins.focusInput()
        if (root.cameIn) {
            root.cameIn = false
            if (root.fingerprintOffered) {
                root.askFingerprint()
            }
        }
    }
    onOpened: {
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
        target: root.contentItem.Window.window
        function onActiveFocusItemChanged() { Qt.callLater(root.reclaimFocus) }
    }
    Connections {
        target: App
        function onInFrontChanged() {
            if (App.inFront) {
                root.showUp()
            } else if (!root.prompting) {
                root.cameIn = true
            }
        }
    }
    Connections {
        target: App
        function onUiUnlockAnswered(unlocked) { pins.answered(unlocked) }
        function onFingerprintAnswered(ok) {
            root.prompting = false
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
            readonly property int kLogoHeight: 44
            readonly property int kLogoGap: 36
            readonly property int kFingerprintGap: 20
            anchors.fill: parent
            anchors.bottomMargin: App.keyboardInset

            PinRow {
                id: pins
                width: Math.min(room.width - 2 * Theme.spacing, implicitWidth)
                height: implicitHeight
                x: (room.width - width) / 2
                y: Math.max(Theme.spacing, Math.min((room.height - height) / 2, room.height - height))
                waitUntil: App.uiWaitUntil
                actionName: qsTr("Unlock the app")
                onSubmitted: (pin) => App.unlockUi(pin)
            }
            Image {
                visible: y >= Theme.spacing
                source: Theme.light ? "qrc:/icon/logo-onlight.svg" : "qrc:/icon/logo.svg"
                width: 230
                height: room.kLogoHeight
                x: (room.width - width) / 2
                y: pins.y - room.kLogoGap - height
                fillMode: Image.PreserveAspectFit
                sourceSize.height: 96
                smooth: true
            }
            IconButton {
                id: fingerprint
                visible: root.fingerprintOffered && y + height <= room.height
                x: (room.width - width) / 2
                y: pins.y + pins.height + room.kFingerprintGap
                iconName: "fingerprint"
                iconSize: 42
                iconWeight: 1.25
                implicitWidth: 72
                implicitHeight: 72
                Accessible.name: qsTr("Unlock by fingerprint")
                onClicked: root.askFingerprint()
            }
        }
    }
}
