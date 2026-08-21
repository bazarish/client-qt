import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Settings that belong to the app rather than to any one profile: the embedded
// I2P router is one engine for every profile, and the privacy switch here
// overrides what each profile is allowed to do. They live in their own window so
// a per-profile setting and an app-wide one can never be read as the same thing.
Popup {
    id: root
    // Back to the page this opened from (Settings); the close button exits.
    signal back()
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
            IconButton { iconName: "back"; font.pixelSize: 26; onClicked: root.back() }
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
                            Label { text: "I2P only, every profile"; color: Theme.text }
                            Label {
                                text: "Refuses clearnet for every profile, whatever each one allows on its "
                                    + "own - this switch wins. A profile whose server publishes no I2P "
                                    + "address goes offline while it is on. Fetching the I2P network "
                                    + "database stays allowed either way: it carries no identity, and "
                                    + "without it there is no I2P to use."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            checked: App.fullPrivacyMode
                            onToggled: App.setFullPrivacyMode(checked)
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Portable mode"; color: Theme.text }
                            Label {
                                text: "Profiles, history and the I2P router's state live in a "
                                    + "bazarish_data folder next to the program instead of your user "
                                    + "folder, so a copy on a stick carries everything with it. "
                                    + "Switching moves what is already there, closes every profile "
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
                    spacing: 8
                    Label { text: "Embedded I2P router"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Label {
                        text: "One router serves every profile: its tunnels, its network database and "
                            + "the addresses each profile is reached at."
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Version"; color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                        Label { text: App.i2pdVersion; color: Theme.text; font.pixelSize: Theme.fontSmall }
                    }
                    MenuButton {
                        Layout.fillWidth: true
                        text: "Router & status…"
                        onClicked: { root.close(); root.showRouterStatus() }
                    }
                }
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
            text: "Every profile closes, the data is moved, and Bazarish has to be started "
                + "again. Nothing is deleted."
        }
    }
}
