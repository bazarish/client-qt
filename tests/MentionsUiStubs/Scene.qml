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
        property string activeOwnName: "Me"
        property var activeContactable: []
        property var editBegun: null
        property int replyCancels: 0
        readonly property QtObject conversation: QtObject {
            function lastOwnText() { return { msgId: 7, e2eId: "e7", text: "my last" } }
        }
        function beginEdit(msgId, e2eId, text) { editBegun = { msgId: msgId, e2eId: e2eId, text: text } }
        function cancelEdit() { editing = false }
        function cancelReply() { replying = false; ++replyCancels }
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
    MemberListSheet {
        objectName: "members"
        session: session
        property string mentioned: ""
        onMentionRequested: (name) => mentioned = name
    }
}
