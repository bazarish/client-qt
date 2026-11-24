// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    property var session: null

    ListModel { id: lines }

    // Held here, not in the delegate: a cleared log rebuilds every delegate.
    property string copiedKey: ""
    property bool allCopied: false
    readonly property int copiedFlashMs: 1500
    readonly property int kOneLineWidth: 600
    readonly property int kWindowWidth: 720

    Timer {
        id: copiedReset
        interval: root.copiedFlashMs
        onTriggered: { root.copiedKey = ""; root.allCopied = false }
    }

    preferredWidth: root.kWindowWidth
    height: parent ? parent.height - Theme.windowTopRoom : implicitHeight

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onConnectionLogUpdated(held) {
            lines.clear()
            for (let i = held.length - 1; i >= 0; --i) {
                lines.append(held[i])
            }
        }
        function onConnectionLogLine(line) {
            lines.insert(0, line)
            while (lines.count > root.session.connectionLogCapacity) {
                lines.remove(lines.count - 1)
            }
        }
    }

    onOpened: if (root.session) { root.session.watchConnectionLog(true) }
    onClosed: if (root.session) { root.session.watchConnectionLog(false) }

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
        for (let i = 0; i < lines.count; ++i) {
            out.push(lineText(lines.get(i)))
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

        WindowHeader {
            title: qsTr("Connection log")
            showBack: root.returnTo !== null
            onBackClicked: root.goBack()
            onCloseClicked: root.close()
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: Theme.contentInset
            Layout.bottomMargin: 0
            text: qsTr("The last %1 events of this account, newest first").arg(lines.count)
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.contentInset
            clip: true
            spacing: 2
            model: lines
            ScrollBar.vertical: ScrollBar {}
            // Read from the list, not the rows: a new line moves the rows under a resting cursor.
            readonly property int hoveredRow: listHover.hovered
                ? rowAt(listHover.point.position, contentY, lines.count) : -1
            function rowAt(position, scrolled, count) {
                return count > 0 ? indexAt(position.x + contentX, position.y + scrolled) : -1
            }
            HoverHandler { id: listHover; cursorShape: Qt.PointingHandCursor }

            delegate: Rectangle {
                id: logRow
                width: list.width
                height: rowLine.implicitHeight + 4
                radius: Theme.radiusSmall
                readonly property bool copied: root.copiedKey === root.lineKey(model)
                color: logRow.copied ? Theme.bubbleOut
                    : (rowTap.pressed ? Theme.pressed
                        : (index === list.hoveredRow ? Theme.rowHover : "transparent"))

                TapHandler { id: rowTap; onTapped: root.copyLine(model) }

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
                        text: root.stamp(model.at)
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        Layout.row: 0
                        Layout.column: 1
                        text: model.outgoing ? "→" : "←"
                        color: model.outgoing ? Theme.accent : Theme.green
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        Layout.row: 0
                        Layout.column: 2
                        Layout.columnSpan: rowLine.stacked ? 2 : 1
                        text: model.what
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.preferredWidth: rowLine.stacked ? -1 : 300
                        Layout.fillWidth: rowLine.stacked
                    }
                    Label {
                        Layout.row: rowLine.stacked ? 1 : 0
                        Layout.column: rowLine.stacked ? 2 : 3
                        text: model.status
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        color: model.status.indexOf("failed") === 0 || model.status === "dropped"
                            ? Theme.danger : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        Layout.row: rowLine.stacked ? 1 : 0
                        Layout.column: rowLine.stacked ? 3 : 4
                        text: logRow.copied ? qsTr("copied") : model.detail
                        color: logRow.copied ? Theme.green : Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: lines.count === 0
                text: qsTr("Nothing yet")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }

        Hairline { }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.contentInset
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
