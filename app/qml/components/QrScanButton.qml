// Bazarish project (c) 2026
import QtQuick
import Bazarish

IconButton {
    id: button
    signal decoded(string text)
    iconName: "qr"
    onClicked: scan.open()
    QrScanSheet {
        id: scan
        onDecoded: function(text) { button.decoded(text) }
    }
}
