import QtQuick
import Bazarish

Item {
    id: root
    property string fingerprint: ""
    property int size: Theme.avatar
    // When true, tapping the avatar opens a large, full-image preview. Off by
    // default so avatars inside a clickable row (chat list, account switcher) keep
    // routing the tap to the row; enabled on the profile/contact/call views.
    property bool enlargeable: false
    width: size
    height: size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceAlt
        clip: true
        Image {
            anchors.fill: parent
            // The avatar provider returns the contact's real photo when one is
            // set, falling back to the deterministic identicon otherwise. The
            // "?r=" suffix is the shared revision: it changes whenever any avatar
            // updates, busting the QML image cache so the new face appears.
            source: root.fingerprint.length > 0
                ? "image://avatar/" + root.fingerprint + "?r=" + Avatars.revision : ""
            sourceSize: Qt.size(parent.width, parent.height)
            smooth: true
            cache: false
        }
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
