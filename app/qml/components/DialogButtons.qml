// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    readonly property int kMinButtonWidth: 104
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool showReject: true
    property string cancelText: qsTr("Cancel")
    property bool showCancel: false
    property bool cancelOnOwnLine: false
    property bool danger: false
    property bool acceptEnabled: true
    signal accepted()
    signal rejected()
    signal cancelled()

    implicitHeight: column.implicitHeight + 28
    implicitWidth: column.implicitWidth + 28

    ColumnLayout {
        id: column
        anchors.right: parent.right
        anchors.left: root.cancelOnOwnLine ? parent.left : undefined
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 14
        anchors.leftMargin: 14
        spacing: 8

        MenuButton {
            visible: root.showCancel && root.cancelOnOwnLine
            Layout.fillWidth: true
            text: root.cancelText
            onClicked: root.cancelled()
        }

        RowLayout {
            Layout.alignment: root.cancelOnOwnLine ? Qt.AlignLeft : Qt.AlignRight
            Layout.fillWidth: root.cancelOnOwnLine
            spacing: 8
            MenuButton {
                visible: root.showCancel && !root.cancelOnOwnLine
                Layout.preferredWidth: Math.max(root.kMinButtonWidth, implicitWidth)
                text: root.cancelText
                onClicked: root.cancelled()
            }
            MenuButton {
                visible: root.showReject
                Layout.fillWidth: root.cancelOnOwnLine
                Layout.preferredWidth: Math.max(root.kMinButtonWidth, implicitWidth)
                text: root.rejectText
                onClicked: root.rejected()
            }
            MenuButton {
                Layout.fillWidth: root.cancelOnOwnLine
                Layout.preferredWidth: Math.max(root.kMinButtonWidth, implicitWidth)
                text: root.acceptText
                danger: root.danger
                enabled: root.acceptEnabled
                onClicked: root.accepted()
            }
        }
    }
}
