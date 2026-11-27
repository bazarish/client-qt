// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Button {
    id: chip
    property var form: ({})
    // The field keeps the focus, and with it the selection this acts on.
    focusPolicy: Qt.NoFocus
    implicitHeight: 24
    padding: 6
    background: Rectangle {
        radius: Theme.radiusSmall
        color: chip.hovered || chip.down ? Theme.surfaceAlt : "transparent"
        border.color: Theme.border
    }
    contentItem: Label {
        text: chip.form.name
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
        verticalAlignment: Text.AlignVCenter
    }
}
