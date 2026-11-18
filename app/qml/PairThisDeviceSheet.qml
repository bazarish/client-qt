// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    property string atRestPassphrase: ""
    property string prefillLink: ""
    property bool refused: false

    readonly property string typed: (linkField.text + linkField.preeditText).trim()
    readonly property string problem: App.pairLinkProblem(root.typed)
    readonly property bool live: App.pairing

    margins: Theme.spacing
    bottomMargin: Theme.spacing + (activeFocus ? App.keyboardInset : 0)
    closePolicy: Popup.CloseOnEscape

    onOpened: {
        linkField.text = root.prefillLink
        codeField.text = ""
        root.refused = false
    }
    onClosed: App.cancelPairing()

    Connections {
        target: App
        function onPairingFinished(ok) {
            if (ok) {
                root.close()
            } else {
                root.refused = true
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Connect this device")
            onCloseClicked: root.close()
        }

        ScrollColumn {
            Layout.fillWidth: true
            Layout.fillHeight: true
            inset: Theme.contentInset
            spacing: 12

            Label {
                visible: !root.live
                text: qsTr("Paste the pairing link, or scan it from the other device")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                visible: !root.live
                Layout.fillWidth: true
                spacing: 8
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea {
                        id: linkField
                        wrapMode: TextArea.WrapAnywhere
                        selectByMouse: true
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        placeholderText: qsTr("Pairing link")
                        background: Rectangle { radius: 8; color: Theme.control; border.color: Theme.controlEdge }
                    }
                }
                QrScanButton {
                    Layout.alignment: Qt.AlignTop
                    onDecoded: function(text) { linkField.text = text }
                }
            }

            Label {
                visible: !root.live && root.problem.length > 0
                text: root.problem
                color: Theme.warn
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            FormField {
                id: codeField
                visible: App.pairNeedsCode
                label: qsTr("Code")
                maximumLength: 4
                inputField.inputMethodHints: Qt.ImhDigitsOnly
                inputField.validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }
            }

            Label {
                visible: App.pairStatus.length > 0
                text: App.pairStatus
                color: root.refused ? Theme.danger : Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            ProgressBar {
                visible: root.live && !App.pairNeedsCode
                Layout.fillWidth: true
                Layout.preferredHeight: 4
                from: 0
                to: 1
                indeterminate: App.pairProgress < 0
                value: App.pairProgress >= 0 ? App.pairProgress : 0
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                MenuButton {
                    visible: root.live
                    iconName: "close"
                    text: qsTr("Cancel")
                    onClicked: App.cancelPairing()
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: goBtn
                    focusPolicy: Theme.pressFocus
                    hoverEnabled: true
                    text: App.pairNeedsCode ? qsTr("Receive") : qsTr("Connect")
                    enabled: App.pairNeedsCode
                        ? codeField.displayText.length === 4
                        : (!root.live && root.typed.length > 0 && root.problem.length === 0)
                    onClicked: {
                        root.refused = false
                        if (App.pairNeedsCode) {
                            App.submitPairCode(codeField.text)
                        } else {
                            App.startPairing(root.typed, root.atRestPassphrase)
                        }
                    }
                    background: Rectangle {
                        radius: 10
                        color: !goBtn.enabled ? Theme.surfaceAlt
                            : (goBtn.hovered ? Theme.accentHover : Theme.accent)
                    }
                    contentItem: Label {
                        text: goBtn.text
                        color: goBtn.enabled ? Theme.accentText : Theme.textDim
                        horizontalAlignment: Text.AlignHCenter
                        leftPadding: 14
                        rightPadding: 14
                    }
                }
            }
        }
    }
}
