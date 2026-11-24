// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Popup {
    id: root
    property int preferredWidth: Theme.windowWidth
    // The window this one replaced; back reopens it.
    property var returnTo: null
    // Not activeFocus: a closing dialog hands the focus back while its keyboard is still up.
    readonly property bool typing: {
        const field = root.contentItem ? root.contentItem.Window.activeFocusItem : null
        if (!field || field.readOnly || !(field instanceof TextInput || field instanceof TextEdit)) {
            return false
        }
        for (let at = field; at; at = at.parent) {
            if (at === root.contentItem) {
                return true
            }
        }
        return false
    }

    function openFrom(parentWindow) {
        root.returnTo = parentWindow
        root.open()
    }
    function goBack() {
        const back = root.returnTo
        root.close()
        if (back) {
            back.open()
        }
    }

    closePolicy: root.returnTo ? Popup.CloseOnPressOutside : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: root.returnTo = null

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(root.preferredWidth, parent ? parent.width - Theme.windowSideRoom : root.preferredWidth)
    height: Math.min(parent ? parent.height - Theme.windowTopRoom : implicitHeight, implicitHeight)
    padding: 0
    background: DialogFrame { }

    Shortcut {
        sequences: [StandardKey.Cancel, "Back"]
        enabled: root.opened && root.returnTo !== null
        onActivated: root.goBack()
    }
}
