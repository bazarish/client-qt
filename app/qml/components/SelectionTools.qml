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
    property var forms: []
    signal formChosen(string open, string close)
    signal pasteRequested()

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

    readonly property bool active: root.field !== null && root.field.activeFocus && root.field.enabled
    readonly property bool hasSelection: root.active && root.field.selectedText.length > 0
    property bool caretMenu: false
    property bool caretShown: false
    property bool dragging: false
    readonly property bool menuShown: root.active && !root.dragging && (root.hasSelection || root.caretMenu)

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
        void [root.field.text, root.field.width, root.view.contentY, root.width, root.height]
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

    readonly property rect startRect: root.active ? root.rectAt(root.field.selectionStart) : Qt.rect(0, 0, 0, 0)
    readonly property rect endRect: root.active ? root.rectAt(root.field.selectionEnd) : Qt.rect(0, 0, 0, 0)
    readonly property rect caretRect: root.active ? root.rectAt(root.field.cursorPosition) : Qt.rect(0, 0, 0, 0)

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
        visible: root.hasSelection && root.inView(root.startRect)
        line: root.startRect
        lean: -1
        onMoved: function(position) {
            const end = root.field.selectionEnd
            root.field.select(end, Math.min(position, end - 1))
        }
    }
    Handle {
        visible: root.hasSelection && root.inView(root.endRect)
        line: root.endRect
        lean: 1
        onMoved: function(position) {
            const start = root.field.selectionStart
            root.field.select(start, Math.max(position, start + 1))
        }
    }
    Handle {
        visible: root.active && !root.hasSelection && (root.caretShown || root.caretMenu) && root.inView(root.caretRect)
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
        x: Math.max(root.kMenuMargin, Math.min(root.width - menu.width - root.kMenuMargin, menu.anchorX - menu.width / 2))
        y: menu.anchorTop - root.kKnob - root.kMenuGap - menu.height
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
                    visible: root.hasSelection
                    onClicked: root.field.cut()
                }
                Action {
                    text: qsTr("Copy")
                    visible: root.hasSelection
                    onClicked: {
                        root.field.copy()
                        root.field.cursorPosition = root.field.selectionEnd
                    }
                }
                Action {
                    text: qsTr("Paste")
                    visible: root.field !== null && (root.field.canPaste || App.clipboardHasImage())
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
                visible: root.hasSelection
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
