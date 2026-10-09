import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Settings that belong to the app rather than to any one account: the embedded
// I2P router is one engine for every account, and the privacy switch here
// overrides what each account is allowed to do. They live in their own window so
// a per-account setting and an app-wide one can never be read as the same thing.
Popup {
    id: root
    // A window of its own: it is opened from several places and belongs to none
    // of them, so it closes rather than going back to one.
    signal showRouterStatus()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    // As tall as what it holds, not a fixed box: there are two settings here.
    height: Math.min(parent ? parent.height - 40 : 520, body.implicitHeight + 60)
    padding: 0
    background: DialogFrame { }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label {
                text: qsTr("Global settings")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                id: body
                width: root.width
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10

                    Label {
                        text: qsTr("Version %1").arg(App.appVersion)
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Interface language")
                            color: Theme.text
                            Layout.fillWidth: true
                        }
                        Dropdown {
                            id: languageBox
                            model: Tr.languages
                            textRole: "name"
                            valueRole: "code"
                            Layout.preferredWidth: implicitWidth
                            Component.onCompleted: currentIndex = indexOfValue(Tr.language)
                            onActivated: Tr.language = currentValue
                            Connections {
                                target: Tr
                                function onLanguageChanged() {
                                    languageBox.currentIndex = languageBox.indexOfValue(Tr.language)
                                }
                            }
                        }
                    }
                    Hairline { Layout.fillWidth: true }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Portable mode"); color: Theme.text }
                            Label {
                                text: qsTr("Accounts, history and the router's state live in a bazarish_data folder next to the program. Switching moves them, closes every account and needs the app started again.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("Now at: %1").arg(App.dataLocation)
                                color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            checked: App.portable
                            onToggled: {
                                portableConfirm.turningOn = checked
                                // Put the switch back until the move is agreed to.
                                checked = App.portable
                                portableConfirm.open()
                            }
                        }
                    }
                }
                Hairline { }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Background tasks"); color: Theme.text }
                            Label {
                                text: qsTr("A panel on the right edge lists the background operations and the stage each one has reached. Off by default.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            checked: App.backgroundTasksVisible
                            onToggled: App.backgroundTasksVisible = checked
                        }
                    }
                }
                Hairline { }

                // One choice, three ways of making it. Each tab carries the
                // description of a transport and its settings, and the switch
                // that turns it on: turning one on turns the others off, which
                // is why there is no way to turn the current one off on its own.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8

                    Label { text: qsTr("How this application connects to I2P"); color: Theme.textDim; font.pixelSize: Theme.fontSmall }

                    TabBar {
                        id: transportTabs
                        Layout.fillWidth: true
                        currentIndex: I2p.transport === "gateway" ? 2
                            : I2p.transport === "sam" ? 1 : 0
                        background: Rectangle {
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.border
                        }
                        ModeTab { text: qsTr("Embedded") }
                        ModeTab { text: qsTr("SAM API") }
                        ModeTab { text: qsTr("Private gateway") }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        currentIndex: transportTabs.currentIndex

                        // The engine inside this application.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("Enabled"); color: Theme.text; Layout.fillWidth: true }
                                Toggle {
                                    checked: I2p.transport === "embedded"
                                    onToggled: {
                                        if (checked) {
                                            I2p.useEmbedded()
                                        } else {
                                            checked = true  // something has to carry the traffic
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: I2p.transport !== "embedded"
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("The I2P backend is chosen at startup. A restart will be needed.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("Version"); color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                                Label { text: App.i2pdVersion; color: Theme.text; font.pixelSize: Theme.fontSmall }
                            }

                            Label {
                                Layout.topMargin: 8
                                text: qsTr("Proxy")
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                text: qsTr("A SOCKS5 proxy the router reaches the I2P network "
                                    + "through. Leave empty to connect straight out.")
                                color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                FormField {
                                    id: proxyHostField
                                    Layout.fillWidth: true
                                    placeholder: qsTr("Host or address")
                                    text: I2p.proxyHost
                                }
                                FormField {
                                    id: proxyPortField
                                    Layout.preferredWidth: 90
                                    placeholder: qsTr("Port")
                                    text: I2p.proxyPort > 0 ? String(I2p.proxyPort) : ""
                                    inputField.validator: IntValidator { bottom: 1; top: 65535 }
                                }
                            }
                            MenuButton {
                                iconName: "check"
                                text: qsTr("Save")
                                Layout.alignment: Qt.AlignRight
                                // An empty proxy is a setting of its own, so what
                                // enables this is a change, not a filled field.
                                enabled: proxyHostField.text.trim() !== I2p.proxyHost
                                    || parseInt(proxyPortField.text || "0") !== I2p.proxyPort
                                onClicked: proxyRestartDialog.open()
                            }
                        }

                        // A router already running on this machine.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("Enabled"); color: Theme.text; Layout.fillWidth: true }
                                Toggle {
                                    id: samToggle
                                    checked: I2p.transport === "sam"
                                    enabled: !I2p.samChecking
                                    onToggled: {
                                        if (checked) {
                                            // The address is saved by the button
                                            // below; this only chooses it.
                                            I2p.useSam(true)
                                            checked = I2p.transport === "sam"
                                        } else {
                                            checked = true  // something has to carry the traffic
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: I2p.transport !== "sam"
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("The I2P backend is chosen at startup. A restart will be needed.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                text: qsTr("Use the local external I2P router.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                FormField {
                                    id: samHostField
                                    Layout.fillWidth: true
                                    placeholder: qsTr("Host or address")
                                    text: I2p.samHost
                                }
                                FormField {
                                    id: samPortField
                                    Layout.preferredWidth: 90
                                    placeholder: qsTr("Port")
                                    text: String(I2p.samPort)
                                    inputField.validator: IntValidator { bottom: 1; top: 65535 }
                                }
                            }
                            Label {
                                visible: root.samNote.length > 0
                                text: root.samNote
                                color: root.samAnswered ? Theme.success : Theme.danger
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            MenuButton {
                                iconName: "check"
                                text: I2p.samChecking ? qsTr("Checking\u2026") : qsTr("Save")
                                Layout.alignment: Qt.AlignRight
                                enabled: !I2p.samChecking
                                    && samHostField.text.trim().length > 0
                                    && (samHostField.text.trim() !== I2p.samHost
                                        || parseInt(samPortField.text || "0") !== I2p.samPort)
                                onClicked: {
                                    root.samNote = ""
                                    I2p.checkAndSaveSam(samHostField.text,
                                        parseInt(samPortField.text || "0"))
                                }
                            }
                        }

                        // A host that runs the router so this device does not.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("Enabled"); color: Theme.text; Layout.fillWidth: true }
                                Toggle {
                                    checked: I2p.transport === "gateway"
                                    enabled: !I2p.gatewayChecking
                                    onToggled: {
                                        if (checked) {
                                            // The address is saved by the button
                                            // below; this only chooses it.
                                            root.gatewayProblem = ""
                                            I2p.useGateway(true)
                                            checked = I2p.transport === "gateway"
                                        } else {
                                            checked = true
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: I2p.transport !== "gateway"
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("The I2P backend is chosen at startup. A restart will be needed.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                text: qsTr("The gateway takes connections over HTTPS, so this application looks like ordinary web traffic. It removes the need for a local I2P router. The gateway sees the I2P addresses you reach, so it has to be one you trust. End-to-end protection of your data is unaffected.")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            FormField {
                                id: gatewayField
                                Layout.fillWidth: true
                                placeholder: "https://host/path#token"
                                text: I2p.gatewayAddress
                            }
                            Label {
                                visible: root.gatewayProblem.length > 0
                                text: root.gatewayProblem
                                color: Theme.danger
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            MenuButton {
                                iconName: "check"
                                text: I2p.gatewayChecking ? qsTr("Checking\u2026") : qsTr("Save")
                                Layout.alignment: Qt.AlignRight
                                enabled: !I2p.gatewayChecking
                                    && gatewayField.text.trim().length > 0
                                    && gatewayField.text.trim() !== I2p.gatewayAddress
                                onClicked: {
                                    root.gatewayProblem = ""
                                    I2p.checkAndSaveGateway(gatewayField.text)
                                }
                            }
                        }
                    }

                    MenuButton {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        iconName: "info"
                        text: qsTr("I2P status")
                        onClicked: { root.close(); root.showRouterStatus() }
                    }
                }
            }
        }
    }

    // Why the last gateway address was refused, cleared when a new one is tried.
    property string gatewayProblem: ""
    // The verdict on the SAM address: whether the router at it answered.
    property string samNote: ""
    property bool samAnswered: false
    Connections {
        target: I2p
        function onGatewayRefused(reason) { root.gatewayProblem = reason }
        function onGatewaySaved() { root.gatewayProblem = "" }
        function onSamRefused(reason) {
            root.samAnswered = false
            root.samNote = reason
        }
        function onSamSaved() {
            root.samAnswered = true
            root.samNote = qsTr("The router answered")
        }
    }

    ProxyRestartDialog {
        id: proxyRestartDialog
        onAnswered: (restartNow) => I2p.saveProxy(
            proxyHostField.text, parseInt(proxyPortField.text || "0"), restartNow)
    }

    // Moving the data is not something to do on a stray tap.
    Dialog {
        id: portableConfirm
        property bool turningOn: false
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: DialogFrame { }
        header: Label {
            text: portableConfirm.turningOn ? qsTr("Move data beside the app?") : qsTr("Move data back?")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: qsTr("Move")
            onAccepted: portableConfirm.accept()
            onRejected: portableConfirm.reject()
        }
        onAccepted: App.setPortable(portableConfirm.turningOn)
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.textDim
            padding: 14
            text: qsTr("Every account closes, the data is moved, and Bazarish has to be started again.")
        }
    }
}
