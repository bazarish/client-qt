import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// What this account said on the wire and what came back: server calls with their
// status, mail to correspondents with the far side's answer, and the service
// traffic in between (self-messages, receipts, token batches).
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

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, parent ? parent.width - 24 : 720)
    height: Math.min(parent ? parent.height - 40 : 600, 560)
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

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

    function asText() {
        let out = []
        for (let i = 0; i < root.lines.length; ++i) {
            const line = root.lines[i]
            out.push(stamp(line.at) + (line.outgoing ? "  ->  " : "  <-  ") + line.what
                + (line.status ? "  " + line.status : "")
                + (line.detail ? "  " + line.detail : ""))
        }
        return out.join("\n")
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { iconName: "back"; font.pixelSize: 26; onClicked: root.back() }
            Label {
                text: "Connection log"
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        Label {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 0
            text: "The last " + root.lines.length + " events of this account, newest first. "
                + "Polls that brought nothing are left out. No message text is kept here."
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

            delegate: RowLayout {
                width: list.width
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
                    text: modelData.detail
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.lines.length === 0
                text: "Nothing yet."
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            MenuButton {
                Layout.fillWidth: true
                text: "Copy all"
                onClicked: if (root.session) { root.session.copyText(root.asText()) }
            }
            MenuButton {
                Layout.fillWidth: true
                text: "Clear"
                onClicked: if (root.session) { root.session.clearConnectionLog() }
            }
        }
    }
}
