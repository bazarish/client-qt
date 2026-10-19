import QtQuick
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

    // `clip` cuts to the bounding box, not to the radius, so the picture is drawn
    // through a mask.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceAlt
    }
    Image {
        id: face
        anchors.fill: parent
        // The provider returns the contact's real photo when one is set and the
        // deterministic identicon otherwise, cut to the circle every avatar here
        // is: the cut belongs there because a mask in the scene graph is a shader
        // effect, and the software renderer draws none. The "?r=" suffix is the
        // shared revision - it changes whenever any avatar updates, busting the
        // QML image cache so the new face appears.
        source: root.fingerprint.length > 0
            ? "image://avatar/" + root.fingerprint + "?r=" + Avatars.revision + "&round=1" : ""
        sourceSize: Qt.size(root.size, root.size)
        smooth: true
        asynchronous: true
        // Cached: the revision in the URL is what busts it, so a face already
        // drawn is reused instead of being redrawn every time a list rebinds.
        cache: true
    }
    // On the light screen an identicon's pale ground would melt into it without an edge.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: Theme.light
        color: "transparent"
        border.color: Theme.border2
        border.width: 1
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
