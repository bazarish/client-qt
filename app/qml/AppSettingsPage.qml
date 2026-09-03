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
    // What this account keeps on this machine, and trimming it.
    signal showStorage()

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
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: "Storage"; color: Theme.text }
                        Label {
                            text: "What this account keeps on this machine, chat by chat, with "
                                + "the pictures and voice notes counted in. Old history can be "
                                + "dropped a chat at a time, keeping the newest messages - here "
                                + "only, on this device."
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        Label {
                            visible: App.session === null
                            text: "Open an account to see what it keeps."
                            color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                    MenuButton {
                        Layout.fillWidth: true
                        text: "Storage…"
                        enabled: App.session !== null
                        onClicked: { root.close(); root.showStorage() }
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

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        // Still linked into this application, but carrying nothing
                        // while a router outside it does the work.
                        enabled: !I2p.samEnabled
                        opacity: enabled ? 1 : 0.4
                        Label { text: "Embedded I2P router"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        Label {
                            text: "One router serves every account: its tunnels, its network database and "
                                + "the addresses each account is reached at."
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Version"; color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                            Label { text: App.i2pdVersion; color: Theme.text; font.pixelSize: Theme.fontSmall }
                        }
                    }

                    // Which router carries the traffic. The engine above unless
                    // this is turned on, and either way the choice takes hold at
                    // the next start.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "SAM API"; color: Theme.text }
                            Label {
                                text: "Use an I2P router already running on this machine "
                                    + "instead of the one above. Takes effect after the "
                                    + "application is restarted."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            id: samToggle
                            checked: I2p.samEnabled
                            onToggled: samRestartDialog.open()
                        }
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
                        text: "Save"
                        Layout.alignment: Qt.AlignRight
                        onClicked: samRestartDialog.open()
                    }

                    MenuButton {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: "I2P status…"
                        onClicked: { root.close(); root.showRouterStatus() }
                    }
                }
            }
        }
    }

    // Swapping the transport is not something this process can do while it runs:
    // the embedded engine cannot be started a second time in one process. So the
    // choice is saved and the application closes.
    SamRestartDialog {
        id: samRestartDialog
        samOn: samToggle.checked
        host: samHostField.text
        port: parseInt(samPortField.text || "0")
        // Nothing was saved, so the switch goes back to what is in force.
        onCancelled: samToggle.checked = I2p.samEnabled
        onAnswered: (closeNow) => {
            I2p.saveSam(samToggle.checked, samHostField.text,
                parseInt(samPortField.text || "0"))
            if (closeNow) {
                Qt.quit()
            }
        }
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
