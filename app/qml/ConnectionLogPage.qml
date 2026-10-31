// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    signal back()

    property var lines: []

    // Held here, not in the delegate: every poll hands a new array, which rebuilds every delegate.
    property string copiedKey: ""
    property bool allCopied: false
    readonly property int copiedFlashMs: 1500
    readonly property int kOneLineWidth: 600

    Timer {
        id: copiedReset
        interval: root.copiedFlashMs
        onTriggered: { root.copiedKey = ""; root.allCopied = false }
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, parent ? parent.width - 24 : 720)
    height: Math.min(parent ? parent.height - 40 : 600, 560)
    padding: 0
    background: DialogFrame { }

    Timer {
        id: poll
        interval: 500
        repeat: true
        running: root.visible
        onTriggered: if (root.session) { root.session.refreshConnectionLog() }
    }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onConnectionLogUpdated(lines) { root.lines = lines.slice().reverse() }
    }

    onOpened: if (root.session) { root.session.refreshConnectionLog() }

    function stamp(millis) {
        return Qt.formatDateTime(new Date(millis), "hh:mm:ss")
    }

    function lineKey(line) {
        return line.at + "|" + line.what
    }

    function lineText(line) {
        return stamp(line.at) + (line.outgoing ? "  ->  " : "  <-  ") + line.what
            + (line.status ? "  " + line.status : "")
            + (line.detail ? "  " + line.detail : "")
    }

    function asText() {
        let out = []
        for (let i = 0; i < root.lines.length; ++i) {
            out.push(lineText(root.lines[i]))
        }
        return out.join("\n")
    }

    function copyLine(line) {
        if (!root.session) {
            return
        }
        App.copyText(root.lineText(line))
        root.copiedKey = root.lineKey(line)
        root.allCopied = false
        copiedReset.restart()
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { iconName: "back"; onClicked: root.back() }
            Label {
                text: qsTr("Connection log")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        Label {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 0
            text: qsTr("The last %1 events of this account, newest first.").arg(root.lines.length)
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            clip: true
            spacing: 2
            model: root.lines
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: logRow
                width: list.width
                height: rowLine.implicitHeight + 4
                radius: Theme.radiusSmall
                readonly property bool copied: root.copiedKey === root.lineKey(modelData)
                color: logRow.copied ? Theme.bubbleOut
                    : (rowTap.pressed ? Theme.pressed
                        : (rowHover.hovered ? Theme.surfaceAlt : "transparent"))
                Behavior on color { ColorAnimation { duration: 120 } }

                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: rowTap; onTapped: root.copyLine(modelData) }

                GridLayout {
                    id: rowLine
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    columnSpacing: 8
                    rowSpacing: 2
                    readonly property bool stacked: list.width < root.kOneLineWidth
                    Label {
                        Layout.row: 0
                        Layout.column: 0
                        text: root.stamp(modelData.at)
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        Layout.row: 0
                        Layout.column: 1
                        text: modelData.outgoing ? "→" : "←"
                        color: modelData.outgoing ? Theme.accent : Theme.green
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        Layout.row: 0
                        Layout.column: 2
                        Layout.columnSpan: rowLine.stacked ? 2 : 1
                        text: modelData.what
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.preferredWidth: rowLine.stacked ? -1 : 300
                        Layout.fillWidth: rowLine.stacked
                    }
                    Label {
                        Layout.row: rowLine.stacked ? 1 : 0
                        Layout.column: rowLine.stacked ? 2 : 3
                        text: modelData.status
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        color: modelData.status.indexOf("failed") === 0 || modelData.status === "dropped"
                            ? Theme.danger : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        Layout.row: rowLine.stacked ? 1 : 0
                        Layout.column: rowLine.stacked ? 3 : 4
                        text: logRow.copied ? qsTr("copied") : modelData.detail
                        color: logRow.copied ? Theme.green : Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.lines.length === 0
                text: qsTr("Nothing yet.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }

        Hairline { }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            MenuButton {
                Layout.fillWidth: true
                iconName: "copy"
                text: root.allCopied ? qsTr("Copied") : qsTr("Copy all")
                positive: root.allCopied
                onClicked: {
                    if (!root.session) {
                        return
                    }
                    App.copyText(root.asText())
                    root.copiedKey = ""
                    root.allCopied = true
                    copiedReset.restart()
                }
            }
            MenuButton {
                Layout.fillWidth: true
                iconName: "trash"
                text: qsTr("Clear")
                onClicked: if (root.session) { root.session.clearConnectionLog() }
            }
        }
    }
}
