// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// One row in the background-activity panel: a kind glyph, the operation title, its
// live human-readable status (and optional detail), a determinate progress bar
// when known, and a state badge - a ticking elapsed time while running, a green
// check when done, a red cross when failed.
Item {
    id: row
    // Model roles from OperationListModel.
    required property string kind
    required property string title
    required property string status
    required property string detail
    required property double progress
    required property int state
    required property double startedAt
    // Non-empty when this operation can be stopped (a file transfer).
    required property string cancelId

    implicitHeight: col.implicitHeight + 18

    readonly property color stateColor: state === 2 ? Theme.danger
        : state === 1 ? Theme.green : Theme.textDim

    function iconFor(k) {
        if (k === "contact") return "plus"
        if (k === "send") return "forward"
        if (k === "file-up") return "up"
        if (k === "file-down") return "down"
        if (k === "call") return "call"
        if (k === "service") return "gear"
        if (k === "alias") return "bang"
        return "dot"
    }

    // Live elapsed seconds while running (drives the badge below).
    property int elapsed: 0
    Timer {
        running: row.state === 0
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: row.elapsed = Math.max(0, Math.floor((Date.now() - row.startedAt) / 1000))
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 9
        anchors.bottomMargin: 9
        spacing: 10

        // Kind glyph in a small rounded chip.
        Rectangle {
            Layout.alignment: Qt.AlignTop
            width: 26; height: 26; radius: 6
            color: Theme.surface
            border.color: Theme.border
            Icon {
                anchors.centerIn: parent
                name: row.iconFor(row.kind)
                color: row.state === 2 ? Theme.danger : Theme.green
                size: 14
            }
        }

        ColumnLayout {
            id: col
            Layout.fillWidth: true
            spacing: 3
            Label {
                text: row.title
                color: Theme.text
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: row.detail.length > 0 ? (row.status + "  ·  " + row.detail) : row.status
                color: row.stateColor
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // Determinate progress bar (files); hidden when progress is unknown.
            Rectangle {
                visible: row.progress >= 0
                Layout.fillWidth: true
                Layout.topMargin: 2
                height: 4
                radius: 2
                color: Theme.deep
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, row.progress))
                    height: parent.height
                    radius: 2
                    color: Theme.green
                    Behavior on width { NumberAnimation { duration: 120 } }
                }
            }
        }

        // Stop, for the operations that can be stopped: a running file transfer.
        Button {
            id: stopButton
            visible: row.state === 0 && row.cancelId.length > 0
            hoverEnabled: true
            implicitWidth: 26
            implicitHeight: 22
            // The glyph is centred in the content rectangle, and the default
            // padding of a button leaves less of one than the glyph needs.
            padding: 0
            ToolTip.visible: hovered
            ToolTip.text: "Stop"
            onClicked: if (App.session) { App.session.cancelTransfer(row.cancelId) }
            background: Rectangle {
                radius: 6
                color: stopButton.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Theme.border
            }
            contentItem: Icon {
                name: "close"
                color: Theme.textDim
                size: 12
            }
        }

        // State badge: ticking elapsed while running, else a terminal mark.
        Label {
            visible: row.state === 0
            Layout.alignment: Qt.AlignVCenter
            text: row.elapsed + "s"
            color: row.stateColor
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
        Icon {
            visible: row.state !== 0
            Layout.alignment: Qt.AlignVCenter
            name: row.state === 1 ? "check" : "close"
            color: row.stateColor
            size: 14
        }
    }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
}
