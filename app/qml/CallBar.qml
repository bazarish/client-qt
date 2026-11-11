// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish
import "Format.js" as Format

Rectangle {
    id: root
    property var session: null
    property bool minimized: false
    signal expandRequested()

    readonly property var calling: App.callSession
    readonly property bool elsewhere: !!root.calling && root.calling !== root.session
    readonly property int kPadding: 6
    readonly property int kSideMargin: 12
    readonly property int kIconSize: 16
    readonly property real kPhaseOpacity: 0.7
    property int elapsedTick: 0

    visible: !!root.calling && (root.elsewhere || root.minimized)
    implicitHeight: line.implicitHeight + 2 * root.kPadding
    color: tap.pressed ? Qt.darker(Theme.green, 1.2)
        : (hover.hovered ? Qt.lighter(Theme.green, 1.15) : Theme.green)
    Accessible.role: Accessible.Button
    Accessible.name: root.peerText()

    Timer {
        interval: 1000
        repeat: true
        running: root.visible
        onTriggered: root.elapsedTick++
    }

    function phase() {
        void root.elapsedTick
        if (!root.calling) {
            return ""
        }
        if (root.calling.callState === "outgoing") {
            return qsTr("Calling…")
        }
        if (root.calling.callState === "incoming") {
            return qsTr("Incoming call")
        }
        if (root.calling.callConnectedAtMs <= 0) {
            return qsTr("Connecting…")
        }
        return Format.callClock(root.calling.callConnectedAtMs, Date.now())
    }

    function peerText() {
        if (!root.calling) {
            return ""
        }
        return root.elsewhere
            ? qsTr("%1 (%2)").arg(root.calling.callPeerName).arg(root.calling.displayName)
            : root.calling.callPeerName
    }

    RowLayout {
        id: line
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.kSideMargin
        anchors.rightMargin: root.kSideMargin
        spacing: 8
        Icon {
            name: "call"
            color: Theme.onGreen
            size: root.kIconSize
        }
        Label {
            text: root.phase()
            color: Theme.onGreen
            opacity: root.kPhaseOpacity
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.NoWrap
        }
        Label {
            Layout.fillWidth: true
            text: root.peerText()
            color: Theme.onGreen
            font.weight: Font.Medium
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: Text.ElideRight
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; onTapped: root.expandRequested() }
}
