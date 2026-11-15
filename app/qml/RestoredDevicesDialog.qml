// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    property var chosen: ({})
    readonly property var offered: App.session ? App.session.devicesToReview : []
    readonly property int chosenCount: Object.keys(chosen).length

    function toggle(deviceId, on) {
        const next = Object.assign({}, chosen)
        if (on) {
            next[deviceId] = true
        } else {
            delete next[deviceId]
        }
        chosen = next
    }

    function chooseAll(on) {
        const next = {}
        if (on) {
            for (const device of offered) {
                next[device.deviceId] = true
            }
        }
        chosen = next
    }

    onAboutToShow: chosen = ({})
    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(420, parent ? parent.width - 24 : 420)
    background: DialogFrame { }
    header: Label {
        text: qsTr("Other devices")
        color: Theme.warn
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: root.chosenCount > 0 ? qsTr("Revoke") : qsTr("Keep all")
        danger: root.chosenCount > 0
        rejectText: qsTr("Later")
        onAccepted: {
            root.close()
            if (App.session) {
                App.session.reviewDevices(Object.keys(root.chosen))
            }
        }
        onRejected: root.close()
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("Choose the devices to revoke after the restore")
        }
        Repeater {
            model: root.offered
            delegate: RowLayout {
                id: deviceRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Toggle {
                    checked: root.chosen[deviceRow.modelData.deviceId] === true
                    onToggled: root.toggle(deviceRow.modelData.deviceId, checked)
                }
                Fingerprint {
                    value: deviceRow.modelData.deviceId
                    whole: true
                    font.pixelSize: Theme.fontSmall
                }
                Label {
                    text: deviceRow.modelData.primary ? qsTr("Primary") : qsTr("Secondary")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: qsTr("Added %1").arg(new Date(deviceRow.modelData.issuedAt * 1000)
                        .toLocaleDateString(Qt.locale(), Locale.ShortFormat))
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.offered.length > 1
            spacing: 8
            Toggle {
                checked: root.offered.length > 0 && root.chosenCount === root.offered.length
                onToggled: root.chooseAll(checked)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Revoke all others")
                color: Theme.text
            }
        }
    }
}
