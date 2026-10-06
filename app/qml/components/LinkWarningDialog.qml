import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// What stands between a link in a message and the browser. Following one leaves
// I2P entirely: the request goes out over the ordinary internet from this
// machine, so the address is worth reading before it is followed rather than
// after.
Dialog {
    id: root
    // The address as it stands in the message. What is shown is what is opened:
    // this markup has no way to give a link a label that differs from its target.
    property string address: ""
    // Set when the desktop had nothing to open it with.
    property string failure: ""

    function ask(url) {
        root.failure = ""
        root.address = url
        root.open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(460, parent ? parent.width - 24 : 460)
    background: Rectangle {
        color: Theme.bg
        radius: Theme.radius
        border.color: Theme.warn
        border.width: 2
    }
    header: Label {
        text: qsTr("Leave Bazarish?")
        color: Theme.warn
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: qsTr("Open")
        rejectText: qsTr("Cancel")
        onRejected: root.close()
        onAccepted: {
            // Said out loud rather than assumed: a desktop with nothing
            // registered for http otherwise answers the press with silence.
            if (Qt.openUrlExternally(root.address)) {
                root.close()
            } else {
                root.failure = qsTr("Nothing on this system opened it. Copy the address instead.")
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.preferredHeight: addressText.implicitHeight + 16
            radius: Theme.radiusSmall
            color: Theme.deep
            border.color: Theme.border
            TextEdit {
                id: addressText
                x: 10
                y: 8
                width: parent.width - 20
                text: root.address
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                textFormat: TextEdit.PlainText
                selectionColor: Theme.accent
                selectedTextColor: Theme.bg
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("This is an external link. Your internet provider and the site will see "
                + "your real address.")
        }
        Label {
            visible: root.failure.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: Theme.danger
            text: root.failure
        }
    }
}
