// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

Label {
    id: root
    property string reason: ""
    property var session: null
    property color textColor: Theme.danger
    property bool copied: false

    text: root.copied ? qsTr("Copied to clipboard") : root.reason
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
            App.copyText(root.reason)
            root.copied = true
            copiedFor.restart()
        }
    }
}
