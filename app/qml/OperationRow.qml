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

    implicitHeight: col.implicitHeight + 18

    readonly property color stateColor: state === 2 ? Theme.danger
        : state === 1 ? Theme.green : Theme.textDim

    function glyphFor(k) {
        if (k === "contact") return "＋"
        if (k === "send") return "→"
        if (k === "file-up") return "↑"
        if (k === "file-down") return "↓"
        if (k === "call") return "☎"
        if (k === "group") return "⚙"
        if (k === "service") return "✦"
        return "•"
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
            Label {
                anchors.centerIn: parent
                text: row.glyphFor(row.kind)
                color: row.state === 2 ? Theme.danger : Theme.green
                font.pixelSize: 14
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

        // State badge: ticking elapsed while running, else a terminal mark.
        Label {
            Layout.alignment: Qt.AlignVCenter
            text: row.state === 0 ? (row.elapsed + "s")
                : row.state === 1 ? "✓" : "✕"
            color: row.stateColor
            font.pixelSize: row.state === 0 ? Theme.fontSmall : Theme.fontBody
            font.weight: Font.DemiBold
        }
    }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
}
