// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    property string address: ""
    property string failure: ""
    property bool copied: false
    readonly property int kFieldPadding: 8
    readonly property int kCopySize: 28
    readonly property int kCopyIcon: 16
    readonly property real kCopyOpacity: 0.6
    readonly property int kCopiedMs: 1500

    function ask(url) {
        root.failure = ""
        root.copied = false
        root.address = url
        root.open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
    background: DialogFrame { }
    header: WindowHeader {
        title: qsTr("Open an external site?")
        titleColor: Theme.warn
        showClose: false
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
            Layout.preferredHeight: Math.max(addressText.implicitHeight, root.kCopySize)
                + 2 * root.kFieldPadding
            radius: Theme.radiusSmall
            color: Theme.deep
            border.color: Theme.border
            TextEdit {
                id: addressText
                x: 10
                y: lineCount > 1 ? root.kFieldPadding : (parent.height - height) / 2
                width: parent.width - 20 - root.kCopySize
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
            // One line: level with it. Wrapped: in the top corner, beside the first line.
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: root.kFieldPadding / 2
                y: addressText.lineCount > 1 ? root.kFieldPadding / 2 : (parent.height - height) / 2
                implicitWidth: root.kCopySize
                implicitHeight: root.kCopySize
                iconSize: root.kCopyIcon
                opacity: root.kCopyOpacity
                iconName: root.copied ? "check" : "copy"
                tint: root.copied ? Theme.green : Theme.text
                Accessible.name: root.copied ? qsTr("Copied") : qsTr("Copy link")
                onClicked: {
                    App.copyText(root.address)
                    root.copied = true
                    copiedFor.restart()
                }
                Timer {
                    id: copiedFor
                    interval: root.kCopiedMs
                    onTriggered: root.copied = false
                }
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
