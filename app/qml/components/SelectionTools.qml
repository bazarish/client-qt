// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// A phone's selection handles and menu for one text field. Qt's own sit right
// over the selection, hide anything the app shows there and take no entries.
Item {
    id: root

    property TextEdit field: null
    property Flickable view: null
    // What the menu stays inside; the window when unset.
    property Item bounds: null
    property var forms: []
    signal formChosen(string open, string close)
    signal pasteRequested()
    // A read-only text is done with: copied, tapped beside or its selection gone.
    signal dismissed()

    readonly property int kTarget: 44
    readonly property int kKnob: 16
    readonly property int kBar: 2
    readonly property int kMenuGap: 6
    readonly property int kMenuMargin: 8
    readonly property int kMenuPadding: 4
    readonly property int kRowHeight: 40
    readonly property int kActionPadding: 12
    readonly property int kSpacing: 4
    // Android hides an idle cursor handle after about this long.
    readonly property int kCaretHandleMs: 4000
    // About as long as Android's own long press.
    readonly property real kLongPressSeconds: 0.5
    // Rich text hands its selection over with these instead of a newline and a space.
    readonly property string kLineSeparator: String.fromCharCode(0x2028)
    readonly property string kParagraphSeparator: String.fromCharCode(0x2029)
    readonly property string kNoBreakSpace: String.fromCharCode(0xa0)

    property bool active: root.field !== null && root.field.activeFocus && root.field.enabled
    // The owner's active and its field change apart: neither is read without the other.
    readonly property bool live: root.active && root.field !== null
    readonly property bool editable: root.field !== null && !root.field.readOnly
    readonly property bool hasSelection: root.live && root.field.selectedText.length > 0
    property bool caretMenu: false
    property bool caretShown: false
    property bool dragging: false
    readonly property bool menuShown: root.live && !root.dragging && (root.hasSelection || root.caretMenu)

    function tapped() {
        root.caretMenu = false
        root.caretShown = true
        caretTimer.restart()
    }
    function longPressed() {
        if (!root.hasSelection) {
            root.caretMenu = true
            root.caretShown = true
            caretTimer.restart()
        }
    }

    // The field's geometry in this item; the reads name what moves it.
    function rectAt(position) {
        void [root.field.text, root.field.width, root.view.contentY, root.view.contentHeight,
            root.view.originY, root.width, root.height]
        // While its whole text is replaced the field reports -1 for a moment.
        if (position < 0) {
            return Qt.rect(0, 0, 0, 0)
        }
        const r = root.field.positionToRectangle(position)
        const at = root.field.mapToItem(root, r.x, r.y)
        return Qt.rect(at.x, at.y, r.width, r.height)
    }
    function viewTop() {
        void [root.view.contentY, root.width, root.height]
        return root.view.mapToItem(root, 0, 0).y
    }
    function boundsTop() {
        void [root.width, root.height]
        return root.bounds ? root.bounds.mapToItem(root, 0, 0).y : -root.mapToItem(null, 0, 0).y
    }
    function boundsBottom() {
        return root.boundsTop() + (root.bounds ? root.bounds.height : root.Window.height)
    }
    function plainSelection() {
        return root.field.selectedText.split(root.kParagraphSeparator).join("\n")
            .split(root.kLineSeparator).join("\n").split(root.kNoBreakSpace).join(" ")
    }
    function covers(scenePoint) {
        return [menu, startHandle, endHandle, caretHandle].some(part => part.visible
            && part.contains(part.mapFromItem(null, scenePoint.x, scenePoint.y)))
    }
    function inView(r) {
        const top = root.viewTop()
        return r.y >= top - 1 && r.y + r.height <= top + root.view.height + 1
    }
    function positionUnder(scenePoint) {
        const at = root.field.mapFromItem(null, scenePoint.x, scenePoint.y)
        return root.field.positionAt(at.x, at.y)
    }
    function naturalWidth(flow) {
        let width = 0
        let count = 0
        for (let i = 0; i < flow.children.length; ++i) {
            const item = flow.children[i]
            if (item.visible && item.implicitWidth > 0) {
                width += item.implicitWidth
                ++count
            }
        }
        return width + Math.max(0, count - 1) * flow.spacing
    }

    readonly property rect startRect: root.live ? root.rectAt(root.field.selectionStart) : Qt.rect(0, 0, 0, 0)
    readonly property rect endRect: root.live ? root.rectAt(root.field.selectionEnd) : Qt.rect(0, 0, 0, 0)
    readonly property rect caretRect: root.live ? root.rectAt(root.field.cursorPosition) : Qt.rect(0, 0, 0, 0)

    Connections {
        target: root.field
        function onTextChanged() {
            root.caretMenu = false
            root.caretShown = false
        }
        function onActiveFocusChanged() {
            if (!root.field.activeFocus) {
                root.caretMenu = false
                root.caretShown = false
            }
        }
        function onSelectedTextChanged() {
            if (root.field.selectedText.length > 0) {
                root.caretMenu = false
                root.caretShown = false
            } else if (!root.editable) {
                root.dismissed()
            }
        }
    }
    // Over everything, so it sees a tap the rows below take.
    TapHandler {
        enabled: root.live && !root.editable
        onTapped: function(point) {
            if (!root.covers(point.scenePosition)) {
                root.dismissed()
            }
        }
    }
    Timer {
        id: caretTimer
        interval: root.kCaretHandleMs
        onTriggered: {
            if (!root.dragging && !root.caretMenu) {
                root.caretShown = false
            }
        }
    }

    // A handle's knob sits above the line: below it there is only the keyboard.
    component Handle: Item {
        id: handle
        property rect line
        // -1 leans left of the line point, 1 right, 0 straight above it.
        property int lean: 0
        signal moved(int position)
        signal tapped()
        x: handle.line.x - root.kTarget / 2 + handle.lean * root.kTarget / 2
        y: handle.line.y - root.kTarget
        width: root.kTarget
        height: root.kTarget
        Rectangle {
            x: root.kTarget / 2 - handle.lean * root.kTarget / 2 - root.kBar / 2
            y: handle.height
            width: root.kBar
            height: handle.line.height
            color: root.field ? root.field.selectionColor : "transparent"
        }
        Rectangle {
            x: (root.kTarget - root.kKnob) / 2 - handle.lean * (root.kTarget - root.kKnob) / 2
            y: handle.height - root.kKnob
            width: root.kKnob
            height: root.kKnob
            radius: root.kKnob / 2
            color: root.field ? root.field.selectionColor : "transparent"
        }
        TapHandler {
            onTapped: handle.tapped()
        }
        DragHandler {
            id: drag
            target: null
            property point offset
            onActiveChanged: {
                if (active) {
                    const point = handle.mapToItem(null, root.kTarget / 2 - handle.lean * root.kTarget / 2,
                        handle.height + handle.line.height / 2)
                    drag.offset = Qt.point(point.x - centroid.scenePressPosition.x,
                        point.y - centroid.scenePressPosition.y)
                    caretTimer.stop()
                }
                root.dragging = active
                if (!active) {
                    caretTimer.restart()
                }
            }
            onCentroidChanged: {
                if (active) {
                    handle.moved(root.positionUnder(Qt.point(centroid.scenePosition.x + drag.offset.x,
                        centroid.scenePosition.y + drag.offset.y)))
                }
            }
        }
    }

    component Action: Button {
        id: action
        focusPolicy: Qt.NoFocus
        implicitHeight: root.kRowHeight
        leftPadding: root.kActionPadding
        rightPadding: root.kActionPadding
        background: Rectangle {
            radius: Theme.radiusSmall
            color: action.down ? Theme.surfaceAlt : "transparent"
        }
        contentItem: Label {
            text: action.text
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
        }
    }

    Handle {
        id: startHandle
        visible: root.hasSelection && root.inView(root.startRect)
        line: root.startRect
        lean: -1
        onMoved: function(position) {
            const end = root.field.selectionEnd
            root.field.select(end, Math.min(position, end - 1))
        }
    }
    Handle {
        id: endHandle
        visible: root.hasSelection && root.inView(root.endRect)
        line: root.endRect
        lean: 1
        onMoved: function(position) {
            const start = root.field.selectionStart
            root.field.select(start, Math.max(position, start + 1))
        }
    }
    Handle {
        id: caretHandle
        visible: root.live && root.editable && !root.hasSelection && (root.caretShown || root.caretMenu)
            && root.inView(root.caretRect)
        line: root.caretRect
        onMoved: function(position) { root.field.cursorPosition = position }
        onTapped: root.caretMenu = !root.caretMenu
    }

    Rectangle {
        id: menu
        visible: root.menuShown
        readonly property real maxWidth: root.width - 2 * (root.kMenuMargin + root.kMenuPadding)
        readonly property real rowWidth: Math.min(menu.maxWidth,
            Math.max(root.naturalWidth(actions), formRow.visible ? root.naturalWidth(formRow) : 0))
        readonly property bool oneLine: root.hasSelection && root.startRect.y === root.endRect.y
        readonly property real anchorX: !root.hasSelection ? root.caretRect.x
            : (menu.oneLine ? (root.startRect.x + root.endRect.x) / 2 : root.width / 2)
        readonly property real anchorTop: Math.max(root.viewTop(),
            root.hasSelection ? root.startRect.y : root.caretRect.y)
        width: menu.rowWidth + 2 * root.kMenuPadding
        height: rows.height + 2 * root.kMenuPadding
        readonly property real above: menu.anchorTop - root.kKnob - root.kMenuGap - menu.height
        readonly property real below: (root.hasSelection ? root.endRect.y + root.endRect.height
            : root.caretRect.y + root.caretRect.height) + root.kMenuGap
        x: Math.max(root.kMenuMargin, Math.min(root.width - menu.width - root.kMenuMargin, menu.anchorX - menu.width / 2))
        y: menu.above >= root.boundsTop() + root.kMenuMargin ? menu.above
            : Math.min(menu.below, root.boundsBottom() - menu.height - root.kMenuMargin)
        color: Theme.surface
        radius: Theme.radiusSmall
        border.color: Theme.border
        border.width: 1

        Column {
            id: rows
            x: root.kMenuPadding
            y: root.kMenuPadding
            spacing: root.kSpacing
            Flow {
                id: actions
                width: menu.rowWidth
                spacing: root.kSpacing
                Action {
                    text: qsTr("Cut")
                    visible: root.hasSelection && root.editable
                    onClicked: root.field.cut()
                }
                Action {
                    text: qsTr("Copy")
                    visible: root.hasSelection
                    onClicked: {
                        if (root.editable) {
                            root.field.copy()
                            root.field.cursorPosition = root.field.selectionEnd
                        } else {
                            App.copyText(root.plainSelection())
                            root.dismissed()
                        }
                    }
                }
                Action {
                    text: qsTr("Paste")
                    visible: root.editable && (root.field.canPaste || App.clipboardHasImage())
                    onClicked: {
                        root.caretMenu = false
                        root.pasteRequested()
                    }
                }
                Action {
                    text: qsTr("Select all")
                    visible: root.field !== null && root.field.length > 0
                        && root.field.selectedText.length < root.field.length
                    onClicked: root.field.selectAll()
                }
            }
            Flow {
                id: formRow
                visible: root.hasSelection && root.editable && root.forms.length > 0
                width: menu.rowWidth
                spacing: root.kSpacing
                Repeater {
                    model: root.forms
                    delegate: FormatChip {
                        required property var modelData
                        form: modelData
                        implicitHeight: root.kRowHeight
                        onClicked: root.formChosen(modelData.open, modelData.close)
                    }
                }
            }
        }
    }
}
