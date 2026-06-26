import QtQuick
import Bazarish

Item {
    property string fingerprint: ""
    property int size: Theme.avatar
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
            source: fingerprint.length > 0
                ? "image://avatar/" + fingerprint + "?r=" + Avatars.revision : ""
            sourceSize: Qt.size(parent.width, parent.height)
            smooth: true
            cache: false
        }
    }
}
