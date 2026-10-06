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
    background: DialogFrame { }

    // The slider is drawn small: the stock handle is a touch target and swamped
    // a settings row.
    readonly property int kSliderHandle: 14
    readonly property int kSliderTrackHeight: 4
    readonly property int kSliderHeight: 18

    function privacyText(level) {
        if (level === 0) {
            return qsTr("1 hop each way. The fastest and the weakest: one router carries "
                + "the tunnel and learns your address.")
        }
        if (level === 1) {
            return qsTr("1 or 2 hops each way, picked per tunnel.")
        }
        return qsTr("3 hops each way. Slowest to build and to answer.")
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
            IconButton { iconName: "back"; onClicked: root.back() }
            Label { text: qsTr("I2P router"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: root.width
                spacing: 14

                // Live status.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10

                    Label { text: qsTr("Status"); color: Theme.textDim; font.pixelSize: Theme.fontSmall }

                    Label {
                        visible: !I2p.running && !I2p.gatewayEnabled
                        // A router with nobody to ask cannot start the network: it
                        // is the bootstrap it waits for, not its own start-up.
                        text: I2p.samEnabled
                            ? qsTr("No router answering at the address below.")
                            : I2p.knownRouters < I2p.minKnownRouters
                            ? qsTr("No network database yet. The router starts once your server "
                              + "hands it one, the first time an account connects.")
                            : qsTr("Router is starting up.")
                        color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }

                    // With a gateway the router is somewhere else, and where is
                    // the only thing worth saying: a light for a state this
                    // device does not hold would be guessing.
                    Label {
                        visible: I2p.gatewayEnabled
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("The router is at %1.").arg(I2p.gatewayHost)
                        color: Theme.text
                    }

                    RowLayout {
                        visible: I2p.running && !I2p.gatewayEnabled
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 8; implicitHeight: 8; radius: 4
                            color: I2p.ready ? Theme.success : Theme.warn
                        }
                        Label {
                            text: I2p.ready ? qsTr("Running — tunnels are up") : qsTr("Starting — building tunnels…")
                            color: Theme.text; Layout.fillWidth: true
                        }
                    }

                    // A gateway's router is shared by everyone using it: its
                    // network database and its connections describe that machine
                    // and its other clients, not this account. What belongs here
                    // is further down - the destinations this account runs.
                    ColumnLayout {
                        // An external router keeps its own counsel about the
                        // network it is on, so there is nothing truthful to show.
                        visible: I2p.running && !I2p.samEnabled && !I2p.gatewayEnabled
                        Layout.fillWidth: true
                        spacing: 6
                        StatRow { label: qsTr("Routers known"); value: I2p.knownRouters }
                        StatRow { label: qsTr("Floodfills"); value: I2p.floodfills }
                        StatRow { label: qsTr("Inbound tunnels"); value: I2p.inboundTunnels }
                        StatRow { label: qsTr("Outbound tunnels"); value: I2p.outboundTunnels }
                    }
                }
                Hairline { }

                // The destinations this router serves right now. The tunnel counts
                // above are router-wide, so without this it is impossible to tell
                // whether they belong to one address or to six.
                ColumnLayout {
                    visible: I2p.running
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label {
                        text: qsTr("Local destinations (%1)").arg(I2p.destinations.length)
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        visible: I2p.destinations.length === 0
                        text: qsTr("No destination is being served. The router is starting.")
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
                                        // Coming up is amber, going away is grey, serving
                                        // is green: three different things, three colours.
                                        color: modelData.state === "building" ? Theme.warn
                                            : modelData.state === "closing" ? Theme.textDim
                                            : Theme.success
                                    }
                                    Label {
                                        text: modelData.label
                                        color: Theme.text; font.weight: Font.Medium
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                    }
                                }
                                // Its own line: the counts do not fit beside a name that
                                // may already carry an account prefix.
                                Label {
                                    // LeaseSets, not peers: one encrypted address costs two
                                    // of them, so the count is not a headcount of who is on
                                    // the other side.
                                    text: qsTr("%1 \u00b7 tunnels %2 in / %3 out \u00b7 "
                                        + "%4 leasesets")
                                        .arg(modelData.state).arg(modelData.tunnelsIn)
                                        .arg(modelData.tunnelsOut).arg(modelData.leaseSets)
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
                Hairline { }

                // Tunnel hop length. Each hop is another router that has to be
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                // subverted to trace a connection, and another leg of latency.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("Tunnel length"); color: Theme.text; font.weight: Font.Medium }
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
                                text: "min"; color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                text: "middle"; color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                            Label {
                                text: "max"; color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            }
                        }
                        Label {
                            text: root.privacyText(I2p.privacyLevel)
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Calls always use the shortest tunnel to keep latency down.")
                            color: Theme.textFaint; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                }
                Rectangle {
                    visible: I2p.running && !I2p.gatewayEnabled
                    Layout.fillWidth: true; height: 1; color: Theme.border
                }

                // Active direct transport connections (NTCP2 / SSU2 sessions).
                // A gateway's are the gateway's, made on behalf of everyone
                // using it.
                ColumnLayout {
                    visible: I2p.running && !I2p.gatewayEnabled
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Label {
                        text: qsTr("Direct network connections: %1").arg(I2p.transports.length)
                        color: Theme.textDim; font.pixelSize: Theme.fontSmall
                    }
                    Label {
                        visible: I2p.transports.length === 0
                        text: qsTr("No direct transport connections yet.")
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
                Hairline { }

                // libi2pd's own logging - off by default, on demand for debugging.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    visible: !I2p.samEnabled && !I2p.gatewayEnabled
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Show I2P logs"); color: Theme.text }
                            Label {
                                text: qsTr("Surface libi2pd's own logging (debugging).")
                                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            checked: I2p.loggingEnabled
                            onToggled: I2p.loggingEnabled = checked
                        }
                    }
                }
            }
        }
    }
}
