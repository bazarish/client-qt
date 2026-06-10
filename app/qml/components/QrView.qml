import QtQuick
import Bazarish

// Renders `text` as a single QR code. For data too large for one symbol the
// provider returns a blank image and the caller should show the link instead.
Rectangle {
    property string text: ""
    property int dim: 240
    width: dim
    height: dim
    color: "white"
    radius: 8

    Image {
        anchors.fill: parent
        anchors.margins: 8
        source: text.length > 0 ? "image://qr/" + encodeURIComponent(text) : ""
        sourceSize: Qt.size(dim, dim)
        smooth: false
        fillMode: Image.PreserveAspectFit
    }
}
