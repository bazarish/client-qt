// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish
import "Format.js" as Format

Popup {
    id: root
    property string host: ""
    property string label: ""
    readonly property var detail: I2p.destinationDetail
    readonly property bool loaded: detail.host === root.host
    property bool copied: false

    readonly property int kMaxWidth: 460
    readonly property int kMaxHeight: 560
    readonly property int kSideRoom: 24
    readonly property int kTopRoom: 40
    readonly property int kPollMs: 2000
    readonly property int kCopiedMs: 1500
    readonly property int kDot: 8
    readonly property int kRowPadding: 6
    readonly property int kRefreshButton: 32
    readonly property int kRefreshIcon: 16
    readonly property int kSpinMs: 900
    readonly property int kFullTurn: 360
    readonly property string kSeparator: " · "

    function show(destinationHost, destinationLabel) {
        root.host = destinationHost
        root.label = destinationLabel
        I2p.inspectDestination(destinationHost)
        open()
    }

    function stateText(state) {
        return state === "published" ? qsTr("published")
            : state === "ready" ? qsTr("ready")
            : state === "closing" ? qsTr("closing")
            : qsTr("building")
    }

    function tunnelStateText(state) {
        return state === "established" ? qsTr("established")
            : state === "expiring" ? qsTr("expiring")
            : state === "failed" ? qsTr("failed")
            : qsTr("building")
    }

    function tunnelLine(tunnel) {
        const parts = [root.tunnelStateText(tunnel.state)]
        if (tunnel.latencyMs !== undefined) {
            parts.push(qsTr("%1 ms").arg(tunnel.latencyMs))
        }
        parts.push(App.humanBytes(tunnel.bytes))
        return parts.join(root.kSeparator)
    }

    function leaseSetLine(leaseSet) {
        const expiry = leaseSet.expiresIn > 0
            ? qsTr("expires in %1").arg(Format.minutesSeconds(leaseSet.expiresIn))
            : qsTr("expired")
        return expiry + root.kSeparator + qsTr("leases: %1").arg(leaseSet.leases)
    }

    // Declared inside the router page, the sheet would size itself to that page.
    parent: Overlay.overlay
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(root.kMaxWidth, parent ? parent.width - root.kSideRoom : root.kMaxWidth)
    height: Math.min(parent ? parent.height - root.kTopRoom : root.kMaxHeight, root.kMaxHeight)
    padding: 0
    background: DialogFrame { }

    onAboutToShow: {
        root.copied = false
        scroll.contentItem.contentY = 0
    }

    Timer {
        interval: root.kPollMs
        repeat: true
        running: root.opened
        onTriggered: I2p.inspectDestination(root.host)
    }

    Timer { id: copiedFor; interval: root.kCopiedMs; onTriggered: root.copied = false }

    Connections {
        target: I2p
        function onDestinationGone() {
            root.close()
        }
    }

    component Dot: Rectangle {
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: root.kDot
        implicitHeight: root.kDot
        radius: root.kDot / 2
    }

    component DetailRow: Rectangle {
        default property alias content: rowBody.data
        Layout.fillWidth: true
        radius: Theme.radiusSmall
        color: Theme.surface
        border.color: Theme.border
        implicitHeight: rowBody.implicitHeight + 2 * root.kRowPadding
        RowLayout {
            id: rowBody
            anchors.fill: parent
            anchors.margins: root.kRowPadding
            spacing: 6
        }
    }

    component DimLine: Label {
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component TunnelSection: ColumnLayout {
        id: section
        property string title: ""
        property var tunnels: []
        Layout.fillWidth: true
        Layout.margins: 16
        spacing: 8
        DimLine { text: section.title }
        DimLine { visible: section.tunnels.length === 0; text: qsTr("No tunnels yet") }
        Repeater {
            model: section.tunnels
            delegate: DetailRow {
                id: tunnelRow
                required property var modelData
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Dot {
                            color: tunnelRow.modelData.state === "established" ? Theme.greenFill
                                : tunnelRow.modelData.state === "failed" ? Theme.danger
                                : tunnelRow.modelData.state === "expiring" ? Theme.textDim
                                : Theme.warn
                        }
                        Label {
                            text: tunnelRow.modelData.hops.length > 0
                                ? tunnelRow.modelData.hops.join(" → ")
                                : qsTr("No transit routers")
                            color: Theme.text
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    DimLine { text: root.tunnelLine(tunnelRow.modelData) }
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label {
                text: root.loaded ? root.detail.label : root.label
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 14
                visible: root.loaded

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Dot {
                            color: root.detail.state === "building" ? Theme.warn
                                : root.detail.state === "closing" ? Theme.textDim
                                : Theme.greenFill
                        }
                        Label {
                            text: root.stateText(root.detail.state)
                            color: Theme.text
                            Layout.fillWidth: true
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: hostArea.implicitHeight
                        TextArea {
                            id: hostArea
                            anchors.left: parent.left
                            anchors.right: parent.right
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextArea.WrapAnywhere
                            text: root.host
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                            rightPadding: copyHost.width + 4
                            background: Rectangle {
                                color: Theme.surface; radius: Theme.radiusSmall; border.color: Theme.border
                            }
                        }
                        IconButton {
                            id: copyHost
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 2
                            opacity: root.copied ? 1 : 0.6
                            iconName: root.copied ? "check" : "copy"
                            tint: root.copied ? Theme.green : Theme.text
                            onClicked: {
                                App.copyText(root.host)
                                root.copied = true
                                copiedFor.restart()
                            }
                        }
                    }
                }
                Hairline { }

                TunnelSection {
                    title: qsTr("Inbound tunnels")
                    tunnels: root.loaded ? root.detail.inbound : []
                }
                Hairline { }

                TunnelSection {
                    title: qsTr("Outbound tunnels")
                    tunnels: root.loaded ? root.detail.outbound : []
                }
                Hairline { }

                ColumnLayout {
                    id: leaseSection
                    readonly property var leaseSets: root.loaded ? root.detail.leaseSets : []
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    DimLine { text: qsTr("Known leasesets") }
                    DimLine { visible: leaseSection.leaseSets.length === 0; text: qsTr("No leasesets yet") }
                    Repeater {
                        model: leaseSection.leaseSets
                        delegate: DetailRow {
                            id: leaseRow
                            required property var modelData
                            readonly property bool busy: root.loaded
                                && root.detail.refreshing.indexOf(modelData.host) >= 0
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: leaseRow.modelData.host
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                DimLine { text: root.leaseSetLine(leaseRow.modelData) }
                            }
                            IconButton {
                                id: lookUp
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: root.kRefreshButton
                                implicitHeight: root.kRefreshButton
                                iconName: "refresh"
                                iconSize: root.kRefreshIcon
                                enabled: !leaseRow.busy
                                tint: leaseRow.busy ? Theme.green : Theme.textDim
                                onClicked: I2p.refreshLeaseSet(leaseRow.modelData.host)
                                RotationAnimator {
                                    target: lookUp.contentItem
                                    from: 0
                                    to: root.kFullTurn
                                    duration: root.kSpinMs
                                    loops: Animation.Infinite
                                    running: leaseRow.busy
                                    onStopped: lookUp.contentItem.rotation = 0
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
