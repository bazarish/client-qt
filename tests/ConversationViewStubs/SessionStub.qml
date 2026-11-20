// Bazarish project (c) 2026
import QtQuick

QtObject {
    property string accountId: "account"
    property string activePeer: ""
    property string activePeerName: ""
    property int unreadTotal: 0
    property bool activeIsGroup: false
    property bool activeIsChannel: false
    property int activeGroupMembers: 0
    property bool activeMemberListHeld: false
    property int presenceRevision: 0
    property int contactsRevision: 0
    property var activeBotMenu: []
    property bool activeGroupWrite: true
    property bool activeAliasLookup: false
    property var conversation: null
    property bool atNewest: true
    property bool hasMoreOlder: false
    property int saved: 0
    signal contactRequestBusy(string fingerprint)
    signal resendFilePickRequested()
    signal scrollToBottom()
    signal reactionsFlashing()

    function open(peer, which, count, firstId, mode, at) {
        activePeer = peer
        fixture.fill(which, count, firstId, mode, at)
    }
    function isSavedChat(peer) { return false }
    function contactKind(peer) { return "user" }
    function peerTyping(peer) { return false }
    function peerOnline(peer) { return false }
    function saveScroll(peer, anchor, stick) { saved += 1 }
    function markReadThroughRow(row) {}
    function loadOlderMessages() { return 0 }
    function loadNewerMessages() { return 0 }
    function forgetReactionFlash() {}
    function jumpToLatest() { scrollToBottom() }
}
