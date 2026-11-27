// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    property string first: ""
    property bool repeating: false
    property bool differed: false
    signal chosen(string pin)

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
    padding: 0

    background: DialogFrame { }
    header: WindowHeader {
        title: qsTr("Set a password")
        onCloseClicked: root.reject()
    }

    onAboutToShow: {
        root.first = ""
        root.repeating = false
        root.differed = false
        pins.clear()
    }
    onOpened: pins.focusInput()

    contentItem: ColumnLayout {
        spacing: 16
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            horizontalAlignment: Text.AlignHCenter
            text: root.repeating ? qsTr("Repeat the password")
                : (root.differed ? qsTr("The passwords differ") : qsTr("From 1 to 6 digits"))
            color: root.differed && !root.repeating ? Theme.danger : Theme.textDim
            wrapMode: Text.Wrap
        }
        PinRow {
            id: pins
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 20
            Layout.preferredHeight: implicitHeight
            actionName: qsTr("Next")
            onSubmitted: (pin) => {
                if (!root.repeating) {
                    root.first = pin
                    pins.answered(true)
                    return
                }
                pins.answered(pin === root.first)
            }
            onSettled: (ok) => {
                if (!root.repeating) {
                    root.repeating = true
                    root.differed = false
                    return
                }
                if (ok) {
                    const pin = root.first
                    root.first = ""
                    root.chosen(pin)
                    root.accept()
                    return
                }
                root.first = ""
                root.repeating = false
                root.differed = true
            }
        }
    }
}
