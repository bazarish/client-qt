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

    // The slider is drawn small: the stock handle is a touch target and swamped
    // a settings row.
    readonly property int kSliderHandle: 14
    readonly property int kSliderTrackHeight: 4
    readonly property int kSliderHeight: 18

    function privacyText(level) {
        if (level === 0) {
            return "1 hop each way. Fastest, and not real anonymity: a single "
                + "router - anyone may run one - carries your tunnel and learns "
                + "your address along with the timing of everything through it. "
                + "It does not see who you talk to or what you send, but one "
                + "operator is all it takes to start linking traffic to you."
        }
        if (level === 1) {
            return "1 or 2 hops each way, picked per tunnel."
        }
        return "3 hops each way - the depth I2P itself defaults to. Slowest to "
            + "build and to answer."
    }

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
            IconButton { iconName: "back"; font.pixelSize: 26; onClicked: root.back() }
            Label { text: "I2P router"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
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
                        Toggle {
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
                            Label { text: "Show I2P logs"; color: Theme.text }
                            Label {
                                text: "Surface libi2pd's own logging (debugging). Off by default."
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            checked: I2p.loggingEnabled
                            onToggled: I2p.loggingEnabled = checked
                        }
                    }
                    // Tunnel hop length. Each hop is another router that has to be
                    // subverted to trace a connection, and another leg of latency.
                    ColumnLayout {
                        enabled: I2p.enabled
                        opacity: I2p.enabled ? 1 : 0.5
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: "Tunnel length"; color: Theme.text; font.weight: Font.Medium }
                        Slider {
                            id: privacySlider
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            implicitHeight: root.kSliderHeight
                            from: 0
                            to: 2
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: I2p.privacyLevel
                            onMoved: I2p.privacyLevel = value
                            background: Rectangle {
                                x: privacySlider.leftPadding
                                y: privacySlider.topPadding
                                    + (privacySlider.availableHeight - height) / 2
                                width: privacySlider.availableWidth
                                height: root.kSliderTrackHeight
                                radius: height / 2
                                color: Theme.deep
                                border.color: Theme.border2
                                border.width: 1
                                Rectangle {
                                    width: privacySlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: height / 2
                                    color: Theme.green
                                }
                            }
                            handle: Rectangle {
                                x: privacySlider.leftPadding + privacySlider.visualPosition
                                    * (privacySlider.availableWidth - width)
                                y: privacySlider.topPadding
                                    + (privacySlider.availableHeight - height) / 2
                                implicitWidth: root.kSliderHandle
                                implicitHeight: root.kSliderHandle
                                radius: width / 2
                                color: Theme.text
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: -4
                            Label {
                                text: "Min"; color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                text: "Middle"; color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Max"; color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                        }
                        Label {
                            text: root.privacyText(I2p.privacyLevel)
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        Label {
                            text: "Voice calls always use Min: longer tunnels would put "
                                + "audible delay into a live call."
                            color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
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

                // The destinations this router serves right now. The tunnel counts
                // above are router-wide, so without this it is impossible to tell
                // whether they belong to one address or to six.
                ColumnLayout {
                    visible: I2p.enabled && I2p.running
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label {
                        text: "Local destinations (" + I2p.destinations.length + ")"
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        text: "Each address has its own tunnels, in and out, and holds the "
                            + "leasesets it has looked up — that last number is what shows real "
                            + "use. An encrypted address counts twice there: the blinded "
                            + "leaseset and the one inside it."
                        color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Label {
                        visible: I2p.destinations.length === 0
                        text: "No destination is being served — the router is only warming up."
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Repeater {
                        model: I2p.destinations
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            radius: Theme.radiusSmall
                            color: Theme.surface
                            border.color: Theme.border
                            implicitHeight: destCol.implicitHeight + 12
                            ColumnLayout {
                                id: destCol
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle {
                                        Layout.alignment: Qt.AlignVCenter
                                        implicitWidth: 8; implicitHeight: 8; radius: 4
                                        color: modelData.state === "building" ? Theme.warn : Theme.success
                                    }
                                    Label {
                                        text: modelData.label
                                        color: Theme.text; font.weight: Font.Medium
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                    }
                                }
                                // Its own line: the counts do not fit beside a name that
                                // may already carry a profile prefix.
                                Label {
                                    // LeaseSets, not peers: one encrypted address costs two
                                    // of them, so the count is not a headcount of who is on
                                    // the other side.
                                    text: modelData.state + " · tunnels " + modelData.tunnelsIn
                                        + " in / " + modelData.tunnelsOut + " out · "
                                        + modelData.leaseSets + " leasesets"
                                    color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap; Layout.fillWidth: true
                                }
                                Label {
                                    // One line, shortened: the head identifies the address
                                    // and a wrapped 56-character base32 dwarfs the row.
                                    text: modelData.host
                                    color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight; Layout.fillWidth: true
                                }
                            }
                        }
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
                        text: "Routers this one is talking to directly. It relays no transit "
                            + "traffic and is no floodfill, so nothing has a reason to dial it: "
                            + "every session here is one it opened itself. Any that is not is "
                            + "marked \"incoming\"."
                        color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap; Layout.fillWidth: true
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
