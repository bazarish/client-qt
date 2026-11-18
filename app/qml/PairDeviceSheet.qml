// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    property var session: null
    property bool primary: true

    readonly property int kCodeSize: 30
    readonly property string uri: root.session ? root.session.pairUri : ""
    readonly property string code: root.session ? root.session.pairCode : ""
    readonly property bool live: root.session !== null && root.session.pairing

    margins: Theme.spacing
    closePolicy: Popup.CloseOnEscape

    onOpened: if (root.session) { root.session.startPairing(root.primary) }
    onClosed: if (root.session) { root.session.stopPairing() }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("New device")
            onCloseClicked: root.close()
        }

        ScrollColumn {
            Layout.fillWidth: true
            Layout.fillHeight: true
            inset: Theme.contentInset
            spacing: 12

            Label {
                text: qsTr("Scan this QR code on the new device, or paste the link there")
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
                    background: Rectangle { radius: 8; color: Theme.control; border.color: Theme.controlEdge }
                }
            }

            Button {
                id: copyBtn
                focusPolicy: Theme.pressFocus
                property bool copied: false
                hoverEnabled: true
                Layout.fillWidth: true
                enabled: root.uri.length > 0
                text: copied ? qsTr("Copied") : qsTr("Copy link")
                onClicked: { App.copyText(root.uri); copied = true; copiedTimer.restart() }
                background: Rectangle {
                    radius: 10
                    color: copyBtn.copied ? Theme.greenFill
                        : (copyBtn.enabled ? (copyBtn.hovered ? Theme.accentHover : Theme.accent)
                            : Theme.surfaceAlt)
                    Behavior on color { ColorAnimation { duration: 200 } }
                }
                contentItem: IconLabel {
                    name: "copy"
                    color: copyBtn.copied ? Theme.greenInk : (copyBtn.enabled ? Theme.accentText : Theme.textDim)
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
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
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
}
