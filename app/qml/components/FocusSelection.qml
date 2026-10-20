// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// One selection menu for whichever text field a phone has the focus in. It sits in that
// field's popup: a tap on it outside the popup would read as a tap beside and close it.
SelectionTools {
    id: root
    property ApplicationWindow appWindow: null
    readonly property int kAboveEverything: 1000
    readonly property Item focused: root.appWindow ? root.appWindow.activeFocusItem : null
    readonly property bool holdsText: root.focused !== null
        && (root.focused instanceof TextInput || root.focused instanceof TextEdit)
    function hostOf(item) {
        for (let p = item; p !== null && p.parent !== null; p = p.parent) {
            if (p.parent === Overlay.overlay) {
                return p
            }
        }
        return root.appWindow ? root.appWindow.contentItem : null
    }
    function scrollerOf(item) {
        for (let p = item ? item.parent : null; p !== null; p = p.parent) {
            if (p instanceof Flickable) {
                return p
            }
        }
        return item
    }
    parent: root.hostOf(root.field)
    anchors.fill: parent
    z: root.kAboveEverything
    visible: App.mobile
    field: root.holdsText ? root.focused : null
    view: root.scrollerOf(root.field)
    bounds: root.appWindow && root.parent === root.appWindow.contentItem ? null : root.parent
    forms: root.field !== null && root.field.selectionForms !== undefined ? root.field.selectionForms : []
    pastesPictures: root.field !== null && root.field.pasteFromMenu !== undefined
    onFormChosen: function(open, close) { root.field.applyForm(open, close) }
    onPasteRequested: {
        if (root.pastesPictures) {
            root.field.pasteFromMenu()
        } else {
            root.field.paste()
        }
    }
    onDismissed: {
        if (root.field !== null) {
            root.field.deselect()
        }
    }
    Connections {
        target: TextSelection
        // Qt's own field reports its release before the focus it takes on it.
        function onTapped(field, at) {
            field.forceActiveFocus()
            root.tapped()
        }
        function onLongPressed(field, at) {
            field.forceActiveFocus()
            root.longPressed(at)
        }
    }
}
