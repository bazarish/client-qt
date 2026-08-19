import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Shown when a server refuses to subscribe this key (usually "not registered
// yet"). Carries the server's full onboarding message and registration link(s),
// which the user can copy or open. It is top-layer and closes only on the
// explicit button - never on a click-away or Escape - so the links are not lost.
Popup {
    id: root
    property string reason: ""
    property string message: ""
    property var links: []

    function show(reasonText, messageText, linkList) {
        reason = reasonText || ""
        message = messageText || ""
        links = linkList || []
        open()
    }

    modal: true
    closePolicy: Popup.NoAutoClose
    // Sit above any other open popup (settings, sheets) so the error is never
    // covered by another window.
    z: 1000
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, (Overlay.overlay ? Overlay.overlay.width : 520) - 32)
    height: Math.min(helloCol.implicitHeight + topPadding + bottomPadding,
        (Overlay.overlay ? Overlay.overlay.height : 600) - 32)
    padding: 18
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.neon; border.width: 2 }

    // Off-screen helper for copying a link to the system clipboard.
    TextEdit { id: clip; visible: false }
    function copyText(t) {
        clip.text = t
        clip.selectAll()
        clip.copy()
        clip.deselect()
        if (typeof window !== "undefined") window.showToast("Copied")
    }

    contentItem: Flickable {
        id: helloFlick
        contentWidth: width
        contentHeight: helloCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: helloCol
            width: helloFlick.width
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "This server needs registration"
                    color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                }
            }

            Label {
                visible: root.reason.length > 0
                text: root.reason
                color: Theme.warn
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }

            // The server's own message, selectable so it can be copied.
            ScrollView {
                visible: root.message.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, msgArea.implicitHeight + 16)
                TextArea {
                    id: msgArea
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    text: root.message
                    color: Theme.text
                    selectByMouse: true
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                }
            }

            Label {
                visible: root.links.length > 0
                text: "Open one of these to register, then connect again:"
                color: Theme.textDim; font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Repeater {
                model: root.links
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        text: modelData
                        color: Theme.text
                        selectByMouse: true
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    }
                    IconButton { iconName: "copy"; onClicked: root.copyText(modelData) }
                    Button {
                        text: "Open"
                        onClicked: Qt.openUrlExternally(modelData)
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                        contentItem: Label { text: parent.text; color: Theme.accent; leftPadding: 10; rightPadding: 10; horizontalAlignment: Text.AlignHCenter }
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: "Close"
                hoverEnabled: true
                onClicked: root.close()
                background: Rectangle { radius: 10; color: parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent }
                contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }
}
