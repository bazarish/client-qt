// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

// An error line the user can take out of the window: a tap puts it on the
// clipboard and says that it did. Every error a message shows is drawn with
// this, so copying one never depends on which kind of message it happened to -
// a reason that can only be retyped from a screenshot is a reason nobody
// reports.
Label {
    id: root
    // The text to show and to copy. Kept apart from `text`, which becomes the
    // confirmation for a moment after a tap.
    property string reason: ""
    // The session that owns the clipboard call; without one the line is inert.
    property var session: null
    property color textColor: Theme.danger
    property bool copied: false

    text: root.copied ? "Copied to clipboard" : root.reason
    color: root.copied ? Theme.green : root.textColor
    font.pixelSize: Theme.fontSmall
    wrapMode: Text.Wrap

    Timer { id: copiedFor; interval: 1500; onTriggered: root.copied = false }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: {
            if (!root.session) {
                return
            }
            root.session.copyText(root.reason)
            root.copied = true
            copiedFor.restart()
        }
    }
}
