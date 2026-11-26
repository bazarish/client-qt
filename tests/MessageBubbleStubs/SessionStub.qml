// Bazarish project (c) 2026
import QtQuick

QtObject {
    property string accountId: "account"
    property string activePeer: "peer"
    property string activePeerName: "Peer"
    property int unreadTotal: 0
    property bool activeIsGroup: false
    property bool activeIsChannel: false
    property bool activeGroupAdmin: false
    property int activeGroupMembers: 0
    property var activeMembers: []
    property bool activeMemberListHeld: false
    property int presenceRevision: 0
    property int contactsRevision: 0
    property var activeBotMenu: []
    property bool activeGroupWrite: true
    property bool activeAliasLookup: false
    property var conversation: null
    property bool atNewest: true
    property bool hasMoreOlder: false
    property string acceptingContact: ""
    property bool online: true
    property string voicePlaying: ""
    property int voicePositionMs: 0
    property real voiceSpeed: 1
    signal contactRequestBusy(string fingerprint)
    signal resendFilePickRequested()
    signal scrollToBottom()
    signal reactionsFlashing()
    signal botKeySettled(string messageId, bool delivered)
    function isSavedChat(peer) { return false }
    function contactKind(peer) { return "user" }
    function peerTyping(peer) { return false }
    function peerOnline(peer) { return false }
    function contactStatus(peer) { return "" }
    function saveScroll(peer, anchor, stick) { }
    function markReadThroughRow(row) { }
    function loadOlderMessages() { return 0 }
    function loadNewerMessages() { return 0 }
    function forgetReactionFlash() { }
    function jumpToLatest() { scrollToBottom() }
    function contactAgreeing(peer) { return false }
    function contactCanAccept(peer) { return false }
    function letterFill(name) { return "transparent" }
    function letterInk(name) { return "black" }
    function letterOf(name) { return "" }
    function linkPlatesIn(text) { return [] }
    function withoutLinkPlates(text) { return text }
    function replyPreview(id) { return null }
    function fileExists(path) { return false }
    function defaultSaveUrl(name) { return "" }
    function defaultPictureSaveUrl(name) { return "" }
}
