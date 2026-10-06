import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// What this account said on the wire and what came back: server calls with their
// status, mail to correspondents with the far side's answer, and the service
// traffic in between (self-messages, receipts, delivery passes).
//
// A debugging window, so it says what happened rather than what it means. It
// carries no message text and no full fingerprints - it is meant to be
// screenshotted into a bug report.
Popup {
    id: root
    property var session: null
    // Return to the page this opened from (the account window).
    signal back()

    // The tail the core keeps; the window shows it newest first.
    property var lines: []

    // The line that has just been copied, and how long it says so. Held here and
    // not in the delegate: the poll below hands the list a brand-new array every
    // half second, which rebuilds every delegate, and an index is no identity
    // either - newest-first means one new event shifts every row down.
    property string copiedKey: ""
    property bool allCopied: false
    readonly property int copiedFlashMs: 1500

    Timer {
        id: copiedReset
        interval: root.copiedFlashMs
        onTriggered: { root.copiedKey = ""; root.allCopied = false }
    }

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, parent ? parent.width - 24 : 720)
    height: Math.min(parent ? parent.height - 40 : 600, 560)
    padding: 0
    background: DialogFrame { }

    // Live enough to watch a send go out, without a worker thread reaching into
    // the interface: the window asks, the account answers.
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
        // The core keeps them oldest first; the window reads newest first.
        function onConnectionLogUpdated(lines) { root.lines = lines.slice().reverse() }
    }

    onOpened: if (root.session) { root.session.refreshConnectionLog() }

    function stamp(millis) {
        return Qt.formatDateTime(new Date(millis), "hh:mm:ss")
    }

    function lineKey(line) {
        return line.at + "|" + line.what
    }

    // One line as text, for the clipboard. A row and "Copy all" write the same
    // shape because they call the same function.
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

            // A whole row is the click target: this window exists to be quoted in a
            // bug report, and one line of it is usually what is wanted.
            delegate: Rectangle {
                id: logRow
                width: list.width
                height: rowLine.implicitHeight + 4
                radius: Theme.radiusSmall
                readonly property bool copied: root.copiedKey === root.lineKey(modelData)
                color: logRow.copied ? Theme.bubbleOut
                    : (rowTap.pressed ? Theme.border2
                        : (rowHover.hovered ? Theme.surfaceAlt : "transparent"))
                Behavior on color { ColorAnimation { duration: 120 } }

                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: rowTap; onTapped: root.copyLine(modelData) }

                RowLayout {
                    id: rowLine
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 8
                    Label {
                        text: root.stamp(modelData.at)
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        text: modelData.outgoing ? "→" : "←"
                        color: modelData.outgoing ? Theme.accent : Theme.green
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        text: modelData.what
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        // One column, so the eye runs down the kinds rather than
                        // hunting for where each line's status begins.
                        Layout.preferredWidth: 300
                    }
                    Label {
                        text: modelData.status
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        // A refusal is the line the user came here for.
                        color: modelData.status.indexOf("failed") === 0 || modelData.status === "dropped"
                            ? Theme.danger : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                    Label {
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
