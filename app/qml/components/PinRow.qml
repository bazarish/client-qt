// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Item {
    id: root
    property double waitUntil: 0
    property real waitLeft: 0
    readonly property bool waiting: waitRun.running
    property bool keyboardBack: false
    property string actionName: ""
    property bool failed: false
    property bool pending: false
    property var answer: null
    property real phase: 0
    readonly property bool busy: root.pending || wave.running
    signal submitted(string pin)
    signal settled(bool ok)

    readonly property int kCells: App.uiPinMaxDigits
    readonly property int kGap: 8
    readonly property int kMaxCell: 52
    readonly property int kLift: 10
    readonly property int kStepMs: 30
    readonly property int kRiseMs: 80
    readonly property int kFailMs: 2000
    readonly property int kDotSize: 10
    readonly property int kEdge: 2
    readonly property real kWaitingOpacity: 0.4
    readonly property int waveMs: (root.kCells - 1) * root.kStepMs + 2 * root.kRiseMs
    readonly property int kArrowSize: 22
    readonly property int kBarGap: 10
    readonly property int kBarHeight: 3
    readonly property int cell: Math.max(0, Math.min(root.kMaxCell,
        Math.floor((root.width - root.kCells * root.kGap) / (root.kCells + 1))))

    implicitWidth: (root.kCells + 1) * root.kMaxCell + root.kCells * root.kGap
    implicitHeight: root.kLift + root.cell + root.kBarGap + root.kBarHeight

    onWaitUntilChanged: {
        const left = root.waitUntil - Date.now()
        waitRun.stop()
        if (left > 0) {
            waitRun.duration = left
            waitRun.start()
        }
    }
    onWaitingChanged: {
        if (!root.waiting && !root.pending && root.keyboardBack) {
            root.focusInput()
        }
    }
    NumberAnimation {
        id: waitRun
        target: root
        property: "waitLeft"
        from: 1
        to: 0
    }

    property bool keyboardWanted: false
    // Android drops a keyboard asked for before the window holds the input focus, as at a start or after a system prompt.
    function focusInput() {
        root.keyboardBack = true
        field.forceActiveFocus()
        root.keyboardWanted = true
        Qt.callLater(root.raiseKeyboard)
    }
    function raiseKeyboard() {
        const window = root.Window.window
        if (root.keyboardWanted && field.activeFocus && App.inFront && window && window.active) {
            Qt.inputMethod.show()
        }
    }
    Connections {
        target: App
        function onInFrontChanged() { root.raiseKeyboard() }
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() { root.raiseKeyboard() }
    }
    Connections {
        target: Qt.inputMethod
        function onVisibleChanged() {
            if (Qt.inputMethod.visible) {
                root.keyboardWanted = false
            } else if (!root.pending) {
                root.keyboardBack = false
            }
        }
    }
    Connections {
        target: field
        function onActiveFocusChanged() {
            if (!field.activeFocus) {
                root.keyboardWanted = false
            }
        }
    }
    function clear() {
        field.text = ""
        root.failed = false
    }
    function submit() {
        if (field.text.length === 0 || root.waiting || root.busy) {
            return
        }
        root.keyboardBack = Qt.inputMethod.visible
        root.pending = true
        root.failed = false
        wave.restart()
        root.submitted(field.text)
    }
    function answered(ok) {
        root.answer = ok
        root.settle()
    }
    function settle() {
        if (wave.running || root.answer === null) {
            return
        }
        const ok = root.answer
        root.answer = null
        root.pending = false
        field.text = ""
        if (!ok) {
            root.failed = true
            failTimer.restart()
            if (!root.waiting && root.keyboardBack) {
                root.focusInput()
            }
        }
        root.settled(ok)
    }
    function liftOf(index) {
        const t = root.phase * root.waveMs - index * root.kStepMs
        if (t <= 0 || t >= 2 * root.kRiseMs) {
            return 0
        }
        return root.kLift * Math.sin(Math.PI * t / (2 * root.kRiseMs))
    }

    NumberAnimation {
        id: wave
        target: root
        property: "phase"
        from: 0
        to: 1
        duration: root.waveMs
        onFinished: root.settle()
    }
    Timer { id: failTimer; interval: root.kFailMs; onTriggered: root.failed = false }

    TextInput {
        id: field
        width: 1
        height: 1
        opacity: 0
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
            | Qt.ImhNoAutoUppercase
        maximumLength: root.kCells
        validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }
        onTextChanged: {
            if ((root.waiting || root.pending) && text.length > 0) {
                text = ""
                return
            }
            if (text.length > 0) {
                root.failed = false
            }
            if (text.length === root.kCells) {
                root.submit()
            }
        }
        onAccepted: root.submit()
    }

    Row {
        id: cells
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: root.kLift
        spacing: root.kGap
        opacity: root.waiting ? root.kWaitingOpacity : 1

        Repeater {
            model: root.kCells
            Rectangle {
                required property int index
                width: root.cell
                height: root.cell
                transform: Translate { y: -root.liftOf(index) }
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: root.kEdge
                border.color: root.failed ? Theme.danger
                    : (field.activeFocus && index === field.text.length ? Theme.accent : Theme.border2)
                Rectangle {
                    anchors.centerIn: parent
                    visible: index < field.text.length
                    width: root.kDotSize
                    height: width
                    radius: width / 2
                    color: Theme.text
                }
            }
        }

        Rectangle {
            id: go
            readonly property bool ready: field.text.length > 0 && !root.waiting && !root.busy
            width: root.cell
            height: root.cell
            radius: Theme.radiusSmall
            color: goTap.pressed ? Theme.accentDown : Theme.accent
            opacity: go.ready ? 1 : root.kWaitingOpacity
            Accessible.role: Accessible.Button
            Accessible.name: root.actionName
            Icon {
                anchors.centerIn: parent
                name: "arrowRight"
                color: Theme.accentText
                size: root.kArrowSize
            }
            TapHandler {
                id: goTap
                enabled: go.ready
                onTapped: root.submit()
            }
        }
    }

    Rectangle {
        visible: root.waiting
        x: cells.x
        y: cells.y + cells.height + root.kBarGap
        width: cells.width * root.waitLeft
        height: root.kBarHeight
        radius: height / 2
        color: Theme.textDim
    }

    TapHandler {
        onTapped: root.focusInput()
    }
}
