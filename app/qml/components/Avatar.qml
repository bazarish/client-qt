import QtQuick
import QtQuick.Effects
import Bazarish

Item {
    id: root
    property string fingerprint: ""
    property int size: Theme.avatar
    // When true, tapping the avatar opens a large, full-image preview. Off by
    // default so avatars inside a clickable row (chat list, account switcher) keep
    // routing the tap to the row; enabled on the account/contact/call views.
    property bool enlargeable: false
    width: size
    height: size

    // The plate behind a face that has not loaded yet, and the shape every avatar
    // in this application is: a circle. `clip` cannot do it - it clips to the
    // bounding box, not to the radius, which is why a square identicon used to
    // sit inside a round plate - so the picture is drawn through a mask.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceAlt
    }
    Image {
        id: face
        anchors.fill: parent
        // The avatar provider returns the contact's real photo when one is
        // set, falling back to the deterministic identicon otherwise. The
        // "?r=" suffix is the shared revision: it changes whenever any avatar
        // updates, busting the QML image cache so the new face appears.
        source: root.fingerprint.length > 0
            ? "image://avatar/" + root.fingerprint + "?r=" + Avatars.revision : ""
        sourceSize: Qt.size(root.size, root.size)
        smooth: true
        asynchronous: true
        // Cached: the revision in the URL is what busts it, so a face already
        // drawn is reused instead of being redrawn every time a list rebinds.
        cache: true
        // Drawn only through the mask below.
        visible: false
        layer.enabled: true
        layer.smooth: true
    }
    Item {
        id: circle
        anchors.fill: parent
        visible: false
        layer.enabled: true
        // The mask's edge is the avatar's edge, so it is the one thing here that
        // must not be drawn jagged: a layer is rendered into a buffer of its own,
        // which gets no antialiasing unless it is asked for, and a hard-edged mask
        // turns a circle into a staircase.
        layer.samples: 8
        layer.smooth: true
        // Drawn four times larger than it is shown and sampled down: the edge of a
        // circle 44 pixels across has nowhere to put a smooth gradient otherwise.
        layer.textureSize: Qt.size(root.size * 4, root.size * 4)
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            antialiasing: true
            color: "black"
        }
    }
    MultiEffect {
        anchors.fill: parent
        source: face
        maskEnabled: true
        maskSource: circle
        // Without a spread the mask is read as a step: every pixel the circle
        // touched at all becomes fully opaque, and the antialiased edge the mask
        // drew turns back into a staircase. The ramp is what keeps it a circle.
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }

    // Tap to view the avatar full-size (created only on demand, one at a time).
    TapHandler {
        enabled: root.enlargeable && root.fingerprint.length > 0
        onTapped: preview.active = true
    }
    Loader {
        id: preview
        active: false
        sourceComponent: AvatarViewer {
            fingerprint: root.fingerprint
            onClosed: preview.active = false
        }
        onLoaded: item.open()
    }
}
