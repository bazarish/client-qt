// Bazarish project (c) 2026
import QtQuick
import Bazarish

IconButton {
    property Item box: null
    iconName: "qr"
    tint: box && box.scanning ? Theme.green : Theme.text
    onClicked: box.scanning = !box.scanning
}
