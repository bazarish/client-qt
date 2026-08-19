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
    height: Math.min(parent ? parent.height - 40 : 520, 480)
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { iconName: "back"; font.pixelSize: 26; onClicked: root.back() }
            Label {
                text: "All profiles"
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
                width: root.width
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10
                    Label {
                        text: "These apply to every profile in this app, open or not."
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "I2P only, every profile"; color: Theme.text }
                            Label {
                                text: "Refuses every clearnet connection in the whole app. A profile whose "
                                    + "server publishes no I2P address goes offline while this is on — it "
                                    + "overrides each profile's own clearnet switch."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Switch {
                            checked: App.fullPrivacyMode
                            onToggled: App.setFullPrivacyMode(checked)
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
}
