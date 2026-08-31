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
    }
    Item {
        id: circle
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle { anchors.fill: parent; radius: width / 2; color: "black" }
    }
    MultiEffect {
        anchors.fill: parent
        source: face
        maskEnabled: true
        maskSource: circle
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
