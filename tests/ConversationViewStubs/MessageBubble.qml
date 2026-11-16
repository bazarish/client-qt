// Bazarish project (c) 2026
import QtQuick
import QtQuick.Layouts

Item {
    id: bubble
    property var session
    property bool reacting
    property bool highlighted
    property bool choosing
    property bool chosen
    signal deleteRequested(var msgId, string e2eId, bool outgoing)
    signal chosenToggled(var msgId, bool outgoing)
    signal groupActionRequested(string command, string postId, string author)
    signal reactRequested(string e2eId, var anchor, bool leftFirst)
    signal forwardRequested(string e2eId)
    signal quoteRequested(var localId)
    signal selectTextRequested(var text)
    signal imageRequested(string url, var localId, string name)
    signal linkRequested(string url)
    signal aliasRequested(string alias)

    readonly property int kGrowMs: 80
    readonly property int kGrownHeight: 160
    readonly property alias shownText: label.text
    readonly property var shownId: model.msgId
    property bool grown: false
    ListView.onReused: grown = false

    width: ListView.view ? ListView.view.width : 0
    implicitHeight: column.implicitHeight
    height: implicitHeight

    Timer {
        running: model.type === "image" && !bubble.grown
        interval: bubble.kGrowMs
        onTriggered: bubble.grown = true
    }
    ColumnLayout {
        id: column
        width: parent.width
        Text {
            id: label
            Layout.fillWidth: true
            text: model.text
            wrapMode: Text.Wrap
            font.pixelSize: 14
        }
        Item {
            Layout.preferredHeight: bubble.grown ? bubble.kGrownHeight : 0
        }
    }
}
