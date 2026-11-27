// Bazarish project (c) 2026
import QtQuick
Item {
    property string acceptText
    property string rejectText
    property string cancelText
    property bool showCancel
    property bool cancelOnOwnLine
    property bool danger
    signal accepted()
    signal rejected()
    signal cancelled()
}
