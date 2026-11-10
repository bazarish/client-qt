// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(520, parent ? parent.width - 24 : 520)
    margins: Theme.spacing
    padding: 0
    background: DialogFrame {}

    header: Label {
        text: qsTr("Welcome to Bazarish")
        color: Theme.neon
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
        padding: 14
    }

    footer: DialogButtons {
        acceptText: qsTr("Next")
        showReject: false
        onAccepted: root.accept()
    }

    contentItem: ScrollColumn {
        inset: Theme.spacing
        spacing: 8

        Label {
            Layout.fillWidth: true
            leftPadding: 14
            rightPadding: 14
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("A messenger where your anonymity is as natural as sending a message")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Label {
                text: qsTr("Choose the app language")
                color: Theme.text
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Dropdown {
                id: languageBox
                model: Tr.languages
                textRole: "name"
                valueRole: "code"
                Layout.preferredWidth: implicitWidth
                Component.onCompleted: currentIndex = indexOfValue(Tr.language)
                onActivated: Tr.language = currentValue
            }
        }
    }
}
