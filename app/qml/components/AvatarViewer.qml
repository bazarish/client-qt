import QtQuick
import QtQuick.Controls
import Bazarish

// A full-size avatar preview: the same face the thumbnail shows (a real photo or
// the deterministic identicon), rendered large and centered over the whole
// window. Square, so the complete image is visible rather than the circular
// thumbnail crop. Tap the image, tap outside, or press Esc to dismiss.
Popup {
    id: viewer
    property string fingerprint: ""

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 10

    // A square box bounded to the smaller window dimension, so it fits any screen.
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
        // Request a large render: the provider scales the stored photo (or draws
        // the identicon) to this size, so the full format looks crisp, not a
        // stretched thumbnail. The "?r=" revision busts the cache like the thumbnail.
        source: viewer.fingerprint.length > 0
            ? "image://avatar/" + viewer.fingerprint + "?r=" + Avatars.revision : ""
        sourceSize: Qt.size(512, 512)
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: false
        TapHandler { onTapped: viewer.close() }
    }
}
