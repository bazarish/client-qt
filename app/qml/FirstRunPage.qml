// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "firstRun"

    signal finished()

    readonly property int kColumnWidth: 420
    readonly property int kStepLanguage: 0
    readonly property int kStepGateway: 1
    property int step: root.kStepLanguage
    readonly property bool busy: I2p.gatewayChecking
    property string problem: ""

    function saveGateway() {
        root.problem = ""
        I2p.checkAndSaveGateway(addressField.text)
    }

    Connections {
        target: I2p
        enabled: root.step === root.kStepGateway
        function onGatewaySaved() {
            I2p.useGateway(true)
            root.finished()
        }
        function onGatewayRefused(reason) {
            root.problem = reason
        }
    }

    component PageButton: Button {
        id: button
        property bool primary: false
        focusPolicy: Theme.pressFocus
        hoverEnabled: true
        background: Rectangle {
            radius: 10
            color: !button.primary ? Theme.surface
                : !button.enabled ? Theme.surfaceAlt
                : button.hovered ? Theme.accentHover : Theme.accent
            border.color: button.primary ? "transparent" : Theme.border
        }
        contentItem: Label {
            text: button.text
            color: !button.primary ? Theme.accent : button.enabled ? Theme.accentText : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.weight: Font.Medium
        }
    }

    ScrollColumn {
        visible: root.step === root.kStepLanguage
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: languageBar.top
        centered: true
        columnWidth: Math.min(availableWidth - 2 * Theme.spacing, root.kColumnWidth)
        spacing: 24

        Label {
            text: qsTr("Welcome to Bazarish")
            color: Theme.green
            font.pixelSize: 24
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
        Dropdown {
            model: Tr.languages
            textRole: "name"
            valueRole: "code"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: implicitWidth
            Component.onCompleted: currentIndex = indexOfValue(Tr.language)
            onActivated: Tr.language = currentValue
        }
    }
    RowLayout {
        id: languageBar
        visible: root.step === root.kStepLanguage
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.spacing
        width: Math.min(parent.width - 2 * Theme.spacing, root.kColumnWidth)
        PageButton {
            primary: true
            text: qsTr("Next")
            Layout.fillWidth: true
            onClicked: root.step = root.kStepGateway
        }
    }

    ScrollColumn {
        visible: root.step === root.kStepGateway
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: gatewayBar.top
        centered: true
        columnWidth: Math.min(availableWidth - 2 * Theme.spacing, root.kColumnWidth)
        spacing: 16

        Label {
            text: qsTr("Use an external gateway?")
            color: Theme.green
            font.pixelSize: 24
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("If you run your own gateway or a friend shared a link to one, enter its address. A gateway lowers the load on the processor, which matters for devices that run on battery. It also helps when you talk to family and friends on the same server. If there is no link or you do not trust whoever gave it, skip this step: the app will run its built-in I2P router.")
        }
        RowLayout {
            Layout.fillWidth: true
            FormField {
                id: addressField
                Layout.fillWidth: true
                enabled: !root.busy
                label: qsTr("Address")
                placeholder: "https://bazari.sh/#gateway?..."
                inputField.onAccepted:
                    if (!root.busy && addressField.text.trim().length > 0) {
                        root.saveGateway()
                    }
            }
            QrScanButton {
                enabled: !root.busy
                Layout.alignment: Qt.AlignBottom
                onDecoded: function(text) { addressField.text = text }
            }
        }
        Label {
            visible: root.problem.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: root.problem
            color: Theme.danger
            font.weight: Font.Medium
        }
    }
    RowLayout {
        id: gatewayBar
        visible: root.step === root.kStepGateway
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.spacing
        width: Math.min(parent.width - 2 * Theme.spacing, root.kColumnWidth)
        spacing: Theme.spacing
        PageButton {
            text: qsTr("Skip")
            enabled: !root.busy
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 1
            Layout.preferredWidth: 1
            onClicked: {
                I2p.skipGateway()
                root.finished()
            }
        }
        PageButton {
            primary: true
            text: root.busy ? qsTr("Checking...") : qsTr("OK")
            enabled: !root.busy && addressField.displayText.trim().length > 0
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 1
            Layout.preferredWidth: 1
            onClicked: root.saveGateway()
        }
    }
}
