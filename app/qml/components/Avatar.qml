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
            source: fingerprint.length > 0 ? "image://identicon/" + fingerprint : ""
            sourceSize: Qt.size(parent.width, parent.height)
            smooth: true
        }
    }
}
