// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// The background-activity overlay: a small translucent handle pinned to the right
// edge (centre), shown only while at least one operation is running, that opens a
// panel sliding in from the right. The panel lists every long-running async
// operation - a contact add, a message or file transfer, a call - with a live
// human-readable status, so a slow operation reads as progress instead of a frozen
// UI. Driven by App.session.operations (the model) and activeOperations (count).
Item {
    id: overlay
    anchors.fill: parent

    property bool open: false
    // Account-level work - restoring a backup - has no session behind it and
    // still belongs here, so the panel lists both and the handle counts both.
    readonly property int active: (App.session ? App.session.activeOperations : 0)
        + App.activeOperations

    // Closing the session (sign-out) tears the panel down.
    Connections {
        target: App
        function onSessionChanged() { overlay.open = false }
    }

    // The right-edge handle: translucent until hovered, hidden when nothing is
    // running (unless the panel is open). Tapping it opens the panel.
    Rectangle {
        id: handle
        visible: overlay.active > 0 && !overlay.open
        width: 30
        height: 72
        radius: 8
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        color: handleArea.containsMouse ? Theme.surfaceAlt : Theme.surface
        border.color: Theme.green
        border.width: 1
        opacity: handleArea.containsMouse ? 1.0 : 0.4
        Behavior on opacity { NumberAnimation { duration: 150 } }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 3
            // A small activity glyph over the running count.
            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "refresh"
                color: Theme.green
                size: 16
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: overlay.active
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
            }
        }
        MouseArea {
            id: handleArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: overlay.open = true
        }
    }

    // Dim scrim behind the open panel; tapping it closes the panel.
    MouseArea {
        anchors.fill: parent
        enabled: overlay.open
        visible: overlay.open
        onClicked: overlay.open = false
        Rectangle { anchors.fill: parent; color: "#000000"; opacity: overlay.open ? 0.35 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } } }
    }

    // The slide-out panel.
    Rectangle {
        id: panel
        width: Math.min(380, overlay.width)
        height: parent.height
        y: 0
        x: overlay.open ? parent.width - width : parent.width
        color: Theme.bg
        border.color: Theme.border
        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // Swallow clicks so they do not fall through to the scrim and close it.
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                Label {
                    text: "Background activity"
                    color: Theme.green
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                IconButton { iconName: "close"; onClicked: overlay.open = false }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // Empty state.
            Label {
                visible: opsList.count === 0
                Layout.fillWidth: true
                Layout.margins: 24
                horizontalAlignment: Text.AlignHCenter
                text: "Nothing running right now."
                color: Theme.textDim
                wrapMode: Text.Wrap
            }

            // Account-level rows first: a restore is what everything else is
            // waiting for while it runs.
            ListView {
                id: accountOps
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                clip: true
                spacing: 2
                interactive: false
                model: App.operations
                delegate: OperationRow { width: accountOps.width }
            }

            ListView {
                id: opsList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: App.session ? App.session.operations : null
                delegate: OperationRow { width: opsList.width }
            }
        }
    }
}
