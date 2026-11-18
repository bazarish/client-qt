// Bazarish project (c) 2026
import QtQuick
import QtQuick.Layouts
ColumnLayout {
    property string title
    property color titleColor
    property bool showBack: false
    property bool showClose: true
    default property alias tools: tools.data
    signal backClicked()
    signal closeClicked()
    RowLayout { id: tools }
}
