// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Popup {
    id: root
    property int preferredWidth: Theme.windowWidth

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(root.preferredWidth, parent ? parent.width - Theme.windowSideRoom : root.preferredWidth)
    height: Math.min(parent ? parent.height - Theme.windowTopRoom : implicitHeight, implicitHeight)
    padding: 0
    background: DialogFrame { }
}
