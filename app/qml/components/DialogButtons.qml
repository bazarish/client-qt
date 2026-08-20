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
    property string acceptText: "OK"
    property string rejectText: "Cancel"
    property bool showReject: true
    property bool danger: false
    signal accepted()
    signal rejected()

    implicitHeight: row.implicitHeight + 28
    implicitWidth: row.implicitWidth + 28

    RowLayout {
        id: row
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 14
        spacing: 8
        MenuButton {
            visible: root.showReject
            Layout.preferredWidth: Math.max(root.kMinButtonWidth, implicitWidth)
            text: root.rejectText
            onClicked: root.rejected()
        }
        MenuButton {
            Layout.preferredWidth: Math.max(root.kMinButtonWidth, implicitWidth)
            text: root.acceptText
            danger: root.danger
            onClicked: root.accepted()
        }
    }
}
