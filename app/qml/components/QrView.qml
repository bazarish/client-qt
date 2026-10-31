// Bazarish project (c) 2026
import QtQuick
import Bazarish

// The provider draws a blank image for data too large for one symbol.
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
