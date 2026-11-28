// Bazarish project (c) 2026
pragma Singleton
import QtQuick
QtObject {
    property bool mobile: false
    property bool canPickPictures: false
    property int keyboardInset: 0
    signal picturesPicked(var uris)
    function markupPlain(text) { return text }
    function clipboardHasImage() { return false }
    function pickPictures() {}
    function holdFeedback() {}
}
