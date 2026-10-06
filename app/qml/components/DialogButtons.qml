import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Footer button row for the app's modal dialogs, in the shared MenuButton style,
// so dialogs never fall back to the default (Basic) Save / Cancel boxes. Set
// `showReject: false` for a dialog that is dismissed by a back arrow instead.
// Set `danger: true` to mark the accept action as destructive.
Item {
    id: root
    // Every dialog button is at least this wide, so short labels line up; a
    // longer label widens its own button instead of being cut short.
    readonly property int kMinButtonWidth: 104
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool showReject: true
    // A third answer, for a dialog where neither of the other two means "leave
    // things as they were". Off unless asked for.
    property string cancelText: qsTr("Cancel")
    property bool showCancel: false
    // A third answer whose label does not fit beside the other two gets a line
    // of its own rather than pushing them off the dialog.
    property bool cancelOnOwnLine: false
    property bool danger: false
    // A dialog whose input is not yet valid keeps its accept action out of reach
    // rather than answering the press with an error.
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

        // Once the third answer has taken a line of its own, the row below it
        // fills the same width and its buttons share it: two short buttons
        // huddled at one end under a full-width one reads as an afterthought.
        RowLayout {
            id: row
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
