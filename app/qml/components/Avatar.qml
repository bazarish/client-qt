// Bazarish project (c) 2026
import QtQuick
import Bazarish

Item {
    id: root
    property string fingerprint: ""
    property int size: Theme.avatar
    property bool enlargeable: false
    width: size
    height: size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceAlt
    }
    Image {
        anchors.fill: parent
        // Rounded by the provider: a scene-graph mask is a shader effect the software renderer skips.
        source: root.fingerprint.length > 0
            ? "image://avatar/" + root.fingerprint + "?r=" + Avatars.revision + "&round=1" : ""
        sourceSize: Qt.size(root.size, root.size)
        smooth: true
        asynchronous: true
        cache: true
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: Theme.light
        color: "transparent"
        border.color: Theme.border2
        border.width: 1
    }

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
