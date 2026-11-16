// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

ApplicationWindow {
    width: 400
    height: 600
    visible: true
    QtObject {
        id: session
        objectName: "session"
        property var activeMembers: ["Bob", "Annika", "Me", "Anna Maria", "Mark", "Жора", "ёлка", "Елена"]
        property string ownName: "Me"
        property string activePeer: "club"
        property bool online: true
        property bool editing: false
        property bool replying: false
        property string editingText: ""
        property string replyingText: ""
        property string replyingSender: ""
        property var recentEmoji: []
        property string sent: ""
        function mentionAt(before) { return kit.mentionOffer(before, activeMembers, ownName) }
        function mentionText(name) { return kit.mentionText(name) }
        function membersInOrder(names, language) { return kit.namesInOrder(names, language) }
        function membersMatching(names, query) { return kit.namesMatching(names, query) }
        function letterFill(name) { return "gray" }
        function letterInk(name) { return "white" }
        function letterOf(name) { return name.charAt(0) }
        function draftFor(peer) { return "" }
        function keepDraft(peer, text) {}
        function typed() {}
        function stoppedTyping() {}
        function sendText(text) { sent = text }
    }
    ColumnLayout {
        anchors.fill: parent
        Item { Layout.fillWidth: true; Layout.fillHeight: true }
        Composer { objectName: "composer"; session: session }
    }
    MemberListSheet { objectName: "members"; session: session }
}
