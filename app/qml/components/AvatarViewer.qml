// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: viewer
    property string fingerprint: ""
    // The account's own picture: Change and, when there is one, Delete under it.
    property bool own: false
    property bool removable: false
    signal changeRequested()
    signal removeRequested()

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 10

    readonly property int box: {
        const ov = Overlay.overlay
        const limit = ov ? Math.min(ov.width, ov.height) : 400
        return Math.max(180, Math.min(420, limit - 72))
    }
    width: box
    height: box + (viewer.own ? actions.implicitHeight + actionsGap : 0)
    readonly property int actionsGap: 10

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.color: Theme.border
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: viewer.actionsGap
        Image {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: viewer.fingerprint.length > 0
                ? "image://avatar/" + viewer.fingerprint + "?r=" + Avatars.revision : ""
            sourceSize: Qt.size(512, 512)
            fillMode: Image.PreserveAspectFit
            smooth: true
            cache: false
            TapHandler { onTapped: viewer.close() }
        }
        RowLayout {
            id: actions
            visible: viewer.own
            Layout.fillWidth: true
            spacing: viewer.actionsGap
            MenuButton {
                Layout.fillWidth: true
                text: qsTr("Change")
                iconName: "image"
                onClicked: {
                    viewer.close()
                    viewer.changeRequested()
                }
            }
            MenuButton {
                Layout.fillWidth: true
                visible: viewer.removable
                text: qsTr("Delete")
                iconName: "trash"
                danger: true
                onClicked: {
                    viewer.close()
                    viewer.removeRequested()
                }
            }
        }
    }
}
