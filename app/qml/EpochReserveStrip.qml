// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Rectangle {
    id: root
    property var session: null
    signal tapped()

    readonly property int kPadding: 4
    readonly property int kSideMargin: 12

    visible: root.session !== null && root.session.epochReserveLow
    implicitHeight: line.implicitHeight + 2 * root.kPadding
    color: Theme.dangerFill

    Label {
        id: line
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.kSideMargin
        anchors.rightMargin: root.kSideMargin
        text: qsTr("The primary device has been offline for a long time")
        color: Theme.dangerInk
        font.pixelSize: Theme.fontSmall
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    TapHandler { onTapped: root.tapped() }
}
