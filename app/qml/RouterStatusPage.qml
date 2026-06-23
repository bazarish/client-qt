import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Status of the embedded I2P router plus the persistent on/off switch. Counts
// come from libi2pd (netDb size, floodfills, our tunnels); they are live only
// while the router is running (it starts lazily on first I2P use).
Popup {
    id: root
    // Return to the page this opened from (Settings); the close button exits.
    signal back()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(parent ? parent.height - 40 : 600, 560)
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // Poll the router diagnostics while the window is open.
    Timer {
        interval: 2000
        repeat: true
        running: root.opened
        triggeredOnStart: true
        onTriggered: I2p.refresh()
    }

    component StatRow: RowLayout {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        Label { text: label; color: Theme.textDim; Layout.fillWidth: true }
        Label { text: value; color: Theme.text; font.weight: Font.Medium }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { text: "‹"; font.pixelSize: 26; onClicked: root.back() }
            Label { text: "I2P router"; color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: root.width
                spacing: 14

                // Enable / disable.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Embedded I2P"; color: Theme.text; font.weight: Font.Medium }
                            Label {
                                text: "Anonymous transport for messages, calls and files: it carries "
                                    + "your traffic to the server without revealing your IP address. It "
                                    + "stays off until you turn it back on."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Switch {
                            checked: I2p.enabled
                            onToggled: I2p.enabled = checked
                        }
                    }
                    Label {
                        visible: !I2p.enabled
                        text: "I2P is off — you now reach the server over clearnet, so its operator "
                            + "and anyone watching your network can see your IP address and that you "
                            + "use this service. Calls and any server reachable only over I2P stop "
                            + "working until you turn it back on."
                        color: Theme.warn; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    // libi2pd's own logging - off by default, on demand for debugging.
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "Show i2p logs"; color: Theme.text }
                            Label {
                                text: "Surface libi2pd's own logging (debugging). Off by default."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Switch {
                            checked: I2p.loggingEnabled
                            onToggled: I2p.loggingEnabled = checked
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Live status.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10

                    Label { text: "Status"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }

                    Label {
                        visible: I2p.enabled && !I2p.running
                        text: "Router is starting up — it stays on while enabled, warming the "
                            + "network database in the background."
                        color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Label {
                        visible: !I2p.enabled
                        text: "Disabled."
                        color: Theme.textDim
                    }

                    RowLayout {
                        visible: I2p.enabled && I2p.running
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 8; implicitHeight: 8; radius: 4
                            color: I2p.ready ? Theme.success : Theme.warn
                        }
                        Label {
                            text: I2p.ready ? "Running — tunnels are up" : "Starting — building tunnels…"
                            color: Theme.text; Layout.fillWidth: true
                        }
                    }

                    ColumnLayout {
                        visible: I2p.enabled && I2p.running
                        Layout.fillWidth: true
                        spacing: 6
                        StatRow { label: "Routers known"; value: I2p.knownRouters }
                        StatRow { label: "Floodfills"; value: I2p.floodfills }
                        StatRow { label: "Inbound tunnels"; value: I2p.inboundTunnels }
                        StatRow { label: "Outbound tunnels"; value: I2p.outboundTunnels }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Active direct transport connections (NTCP2 / SSU2 sessions).
                ColumnLayout {
                    visible: I2p.enabled && I2p.running
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label {
                        text: "Direct connections (" + I2p.transports.length + ")"
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        visible: I2p.transports.length === 0
                        text: "No direct transport connections yet."
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }
                    Frame {
                        visible: I2p.transports.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        padding: 4
                        background: Rectangle { color: Theme.surface; radius: Theme.radiusSmall; border.color: Theme.border }
                        ListView {
                            id: connList
                            anchors.fill: parent
                            clip: true
                            model: I2p.transports
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Label {
                                width: ListView.view.width
                                padding: 6
                                text: modelData
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
