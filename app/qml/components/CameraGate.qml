// Bazarish project (c) 2026
import QtCore
import QtQuick

// Asked when the camera is first wanted; until answered the system may list no camera.
CameraPermission {
    id: gate
    readonly property bool granted: gate.status === Qt.PermissionStatus.Granted
    readonly property bool refused: gate.status === Qt.PermissionStatus.Denied
    // Asked even when granted: the answer is what refreshes a status read before the grant.
    function ask() {
        gate.request()
    }
}
