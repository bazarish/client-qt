import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// A contact's own card, offered so they can be passed on: a fingerprint alone
// reaches nobody. It is a page of its own because a QR plus a link plus the
// contact's details do not fit one dialog on a short screen, and this way the
// content decides the height instead of being squeezed beside everything else.
Popup {
    id: root
    property var session: null
    // Read when the sheet opens, from routing the session already holds.
    property string link: ""
    property bool copied: false
    // Back to the contact card this came from; the close button exits.
    signal back()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 420
    height: Math.min(parent ? parent.height - 40 : 640, body.implicitHeight + 84)
    padding: 0

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    onOpened: {
        copied = false
        link = (session && session.activePeer.length > 0)
            ? session.contactInvite(session.activePeer) : ""
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { iconName: "back"; onClicked: root.back() }
            Label {
                text: "Share contact"
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
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 14
                    text: root.session
                        ? root.session.contactName(root.session.activePeer)
                        : ""
                    color: Theme.text
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    text: "This is the card they gave you — who they are and where to reach them. "
                        + "Whoever you pass it to learns their address, so pass it on only with "
                        + "their agreement."
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                QrView {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.link.length > 0
                    text: root.link
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.preferredHeight: 96
                    TextArea {
                        id: linkArea
                        readOnly: true
                        wrapMode: TextArea.WrapAnywhere
                        text: root.link
                        placeholderText: "No address held for this contact yet."
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        selectByMouse: true
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    }
                }
                MenuButton {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 14
                    enabled: root.link.length > 0
                    text: root.copied ? "Copied" : "Copy contact link"
                    onClicked: {
                        linkArea.selectAll(); linkArea.copy(); linkArea.deselect()
                        root.copied = true
                        copiedTimer.restart()
                    }
                }
            }
        }
    }
    Timer { id: copiedTimer; interval: 1500; onTriggered: root.copied = false }
}
