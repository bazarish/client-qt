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
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label {
                text: "Global settings"
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

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

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Portable mode"; color: Theme.text }
                            Label {
                                text: "Accounts, history and the I2P router's state live in a "
                                    + "bazarish_data folder next to the program instead of your user "
                                    + "folder, so a copy on a stick carries everything with it. "
                                    + "Switching moves what is already there, closes every account "
                                    + "and needs the app started again."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            Label {
                                text: "Now at: " + App.dataLocation
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
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Background tasks"; color: Theme.text }
                            Label {
                                text: "A handle on the right edge that opens a list of what the "
                                    + "client is doing right now - a contact being added, a message "
                                    + "or file on its way, a call being set up - with the stage each "
                                    + "one has reached. Off by default: it is what to look at when "
                                    + "something seems stuck."
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
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // One choice, three ways of making it. Each tab carries the
                // description of a transport and its settings, and the switch
                // that turns it on: turning one on turns the others off, which
                // is why there is no way to turn the current one off on its own.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8

                    Label { text: "How this application reaches I2P"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }

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
                        ModeTab { text: "Embedded" }
                        ModeTab { text: "SAM API" }
                        ModeTab { text: "Private gateway" }
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
                                Label { text: "Enabled"; color: Theme.text; Layout.fillWidth: true }
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
                                text: "A router inside this application, serving every account on "
                                    + "it: its own tunnels, its own network database, and the "
                                    + "addresses each account is reached at. Nothing outside this "
                                    + "device is trusted with anything."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Version"; color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                                Label { text: App.i2pdVersion; color: Theme.text; font.pixelSize: Theme.fontSmall }
                            }
                        }

                        // A router already running on this machine.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Enabled"; color: Theme.text; Layout.fillWidth: true }
                                Toggle {
                                    id: samToggle
                                    checked: I2p.transport === "sam"
                                    onToggled: {
                                        if (checked) {
                                            I2p.saveSam(true, samHostField.text,
                                                parseInt(samPortField.text || "0"))
                                        } else {
                                            checked = true  // something has to carry the traffic
                                        }
                                    }
                                }
                            }
                            Label {
                                text: "Use an I2P router already running on this machine instead "
                                    + "of the one inside this application. It holds the keys of "
                                    + "every destination it operates, so it has to be yours."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                FormField {
                                    id: samHostField
                                    Layout.fillWidth: true
                                    placeholder: "Host or address"
                                    text: I2p.samHost
                                }
                                FormField {
                                    id: samPortField
                                    Layout.preferredWidth: 90
                                    placeholder: "Port"
                                    text: String(I2p.samPort)
                                    inputField.validator: IntValidator { bottom: 1; top: 65535 }
                                }
                            }
                            MenuButton {
                                iconName: "check"
                                text: "Save"
                                Layout.alignment: Qt.AlignRight
                                onClicked: I2p.saveSam(samToggle.checked, samHostField.text,
                                    parseInt(samPortField.text || "0"))
                            }
                        }

                        // A host that runs the router so this device does not.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Enabled"; color: Theme.text; Layout.fillWidth: true }
                                Toggle {
                                    checked: I2p.transport === "gateway"
                                    enabled: !I2p.gatewayChecking
                                    onToggled: {
                                        if (checked) {
                                            root.gatewayProblem = ""
                                            I2p.checkAndSaveGateway(gatewayField.text)
                                            checked = I2p.transport === "gateway"
                                        } else {
                                            checked = true
                                        }
                                    }
                                }
                            }
                            Label {
                                text: "A host runs the router; this device starts none. It sees "
                                    + "every address you connect to and holds the keys of the "
                                    + "destinations it makes for you. Identity keys stay on this "
                                    + "device. Messages, files and calls stay encrypted end to end."
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
                                text: I2p.gatewayChecking ? "Checking…" : "Save"
                                Layout.alignment: Qt.AlignRight
                                enabled: !I2p.gatewayChecking && gatewayField.text.trim().length > 0
                                onClicked: {
                                    root.gatewayProblem = ""
                                    I2p.checkAndSaveGateway(gatewayField.text)
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        wrapMode: Text.Wrap
                        text: "Which one carries the traffic is settled when this application "
                            + "starts, so a change takes effect the next time it runs."
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }

                    MenuButton {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        iconName: "info"
                        text: "I2P status"
                        onClicked: { root.close(); root.showRouterStatus() }
                    }
                }
            }
        }
    }

    // Why the last gateway address was refused, cleared when a new one is tried.
    property string gatewayProblem: ""
    Connections {
        target: I2p
        function onGatewayRefused(reason) { root.gatewayProblem = reason }
        function onGatewaySaved() { root.gatewayProblem = "" }
    }

    // Moving the data is not something to do on a stray tap.
    Dialog {
        id: portableConfirm
        property bool turningOn: false
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(360, parent ? parent.width - 24 : 360)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: portableConfirm.turningOn ? "Move data beside the app?" : "Move data back?"
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Move"
            onAccepted: portableConfirm.accept()
            onRejected: portableConfirm.reject()
        }
        onAccepted: App.setPortable(portableConfirm.turningOn)
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.textDim
            padding: 14
            text: "Every account closes, the data is moved, and Bazarish has to be started "
                + "again. Nothing is deleted."
        }
    }
}
