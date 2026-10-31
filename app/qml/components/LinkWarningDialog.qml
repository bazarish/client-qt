// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    property string address: ""
    property string failure: ""

    function ask(url) {
        root.failure = ""
        root.address = url
        root.open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(460, parent ? parent.width - 24 : 460)
    background: DialogFrame { }
    header: Label {
        text: qsTr("Open an external site?")
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
            text: qsTr("This is an external link. The site will probably learn your IP address.")
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
