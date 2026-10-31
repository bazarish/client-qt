// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Popup {
    id: viewer
    property string fingerprint: ""

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 10

    readonly property int box: {
        const ov = Overlay.overlay
        const limit = ov ? Math.min(ov.width, ov.height) : 400
        return Math.max(180, Math.min(420, limit - 72))
    }
    width: box
    height: box

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.color: Theme.border
        border.width: 1
    }

    contentItem: Image {
        source: viewer.fingerprint.length > 0
            ? "image://avatar/" + viewer.fingerprint + "?r=" + Avatars.revision : ""
        sourceSize: Qt.size(512, 512)
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: false
        TapHandler { onTapped: viewer.close() }
    }
}
