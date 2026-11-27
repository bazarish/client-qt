// Bazarish project (c) 2026
pragma Singleton
import QtQuick

QtObject {
    property bool mobile: false
    property bool uiLocked: false
    property bool inFront: true
    property bool lightTheme: true
    property int unreadElsewhere: 0
    function emojiCount(text) { return 0 }
    function humanBytes(bytes) { return bytes + " B" }
    function inBackGestureEdge(x, width) { return false }
    function isAppLink(link) { return false }
    function markupPlain(text) { return text }
    function markupHtml(text) { return text }
    function copyText(text) { }
    function holdFeedback() { }
    function openLink(link) { }
}
