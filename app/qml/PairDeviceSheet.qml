import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null

    readonly property int kCodeSize: 30
    readonly property string uri: root.session ? root.session.pairUri : ""
    readonly property string code: root.session ? root.session.pairCode : ""
    readonly property bool live: root.session !== null && root.session.pairing

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    padding: 18
    closePolicy: Popup.CloseOnEscape

    onOpened: if (root.session) { root.session.startPairing() }
    onClosed: if (root.session) { root.session.stopPairing() }

    background: DialogFrame { }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("New device")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

        Label {
            text: qsTr("Scan this code on the new device, or paste the link there.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        QrView {
            Layout.alignment: Qt.AlignHCenter
            visible: root.uri.length > 0
            text: root.uri
        }

        ScrollView {
            visible: root.uri.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            TextArea {
                readOnly: true
                wrapMode: TextArea.WrapAnywhere
                selectByMouse: true
                text: root.uri
                color: Theme.text
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
            }
        }

        Button {
            id: copyBtn
            property bool copied: false
            hoverEnabled: true
            Layout.fillWidth: true
            enabled: root.uri.length > 0
            text: copied ? qsTr("Copied") : qsTr("Copy link")
            onClicked: { App.copyText(root.uri); copied = true; copiedTimer.restart() }
            background: Rectangle {
                radius: 10
                color: copyBtn.copied ? Theme.success
                    : (copyBtn.enabled ? (copyBtn.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent)
                        : Theme.surfaceAlt)
                Behavior on color { ColorAnimation { duration: 200 } }
            }
            contentItem: IconLabel {
                name: "copy"
                color: copyBtn.enabled || copyBtn.copied ? Theme.accentText : Theme.textDim
            }
            Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
        }

        Hairline { visible: root.code.length > 0 }

        ColumnLayout {
            visible: root.code.length > 0
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: qsTr("Code for the new device")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            Label {
                text: root.code
                color: Theme.text
                font.pixelSize: root.kCodeSize
                font.weight: Font.DemiBold
            }
        }

        Hairline { }

        Label {
            text: root.session ? root.session.pairStatus : ""
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ProgressBar {
            visible: root.live
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            from: 0
            to: 1
            indeterminate: root.session === null || root.session.pairProgress < 0
            value: root.session && root.session.pairProgress >= 0 ? root.session.pairProgress : 0
        }
    }
}
