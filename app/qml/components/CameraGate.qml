// Bazarish project (c) 2026
import QtCore
import QtQuick

// Asked when the camera is first wanted; until answered the system may list no camera.
CameraPermission {
    id: gate
    readonly property bool granted: gate.status === Qt.PermissionStatus.Granted
    readonly property bool refused: gate.status === Qt.PermissionStatus.Denied
    // On Android a request pauses the window even when granted, and a paused window stops the camera.
    function ask() {
        if (!gate.granted) {
            gate.request()
        }
    }
}
