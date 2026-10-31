// Bazarish project (c) 2026
import QtCore
import QtQuick

MicrophonePermission {
    id: gate
    signal refused()
    property var waiting: null
    function whenGranted(action) {
        if (gate.status === Qt.PermissionStatus.Granted) {
            action()
            return
        }
        if (gate.status === Qt.PermissionStatus.Denied) {
            gate.refused()
            return
        }
        gate.waiting = action
        gate.request()
    }
    onStatusChanged: {
        const action = gate.waiting
        gate.waiting = null
        if (action === null) {
            return
        }
        if (gate.status === Qt.PermissionStatus.Granted) {
            action()
        } else if (gate.status === Qt.PermissionStatus.Denied) {
            gate.refused()
        }
    }
}
