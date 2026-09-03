// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// What one account keeps on this machine, conversation by conversation, and the
// one thing that can be done about it: keep the newest messages of a chat and
// drop the rest. The figures are what the messages and their pictures hold; the
// file is larger, because it also holds indexes, page overhead and free space.
Popup {
    id: root
    // Return to the page this opened from (Global settings); close exits.
    signal back()

    readonly property var session: App.session
    readonly property var info: root.session ? root.session.deviceStorage : ({})
    readonly property bool busy: root.info.busy === true
    readonly property var chats: root.info.chats !== undefined ? root.info.chats : []

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(600, parent ? parent.width - 24 : 600)
    height: Math.min(parent ? parent.height - 40 : 600, 560)
    padding: 0
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // A full pass over the transcript, so it runs when the window opens and when
    // a trim changes what there is to count - never on a timer.
    onOpened: if (root.session) { root.session.measureDeviceStorage() }

    function humanBytes(n) {
        if (!n || n <= 0) {
            return "0 B"
        }
        const u = ["B", "KB", "MB", "GB", "TB"]
        var v = n
        var i = 0
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return (i === 0 ? v : v.toFixed(1)) + " " + u[i]
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { iconName: "back"; font.pixelSize: 26; onClicked: root.back() }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: "Storage"
                    color: Theme.green
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                }
                // Several accounts can be open at once, and this measures one.
                Label {
                    visible: root.session !== null
                    text: root.session ? root.session.displayName : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: root.width
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 14
                    spacing: 4
                    Label {
                        text: "Database file: " + root.humanBytes(root.info.fileBytes)
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    Label {
                        visible: root.info.freeBytes > 0
                        text: "About " + root.humanBytes(root.info.freeBytes)
                            + " of it is free space compacting returns to the disk."
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 8
                        // Removes nothing: it only hands back what deleting
                        // already freed, so it is not a destructive action.
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Compact the database (VACUUM)"
                            enabled: !root.busy && root.session !== null
                            onClicked: root.session.compactDatabase()
                        }
                        MenuButton {
                            Layout.fillWidth: true
                            text: "Trim every chat…"
                            danger: true
                            enabled: !root.busy && root.chats.length > 0
                            onClicked: {
                                trimDialog.peer = ""
                                trimDialog.who = "every chat"
                                trimDialog.open()
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                Label {
                    visible: root.chats.length === 0
                    Layout.fillWidth: true
                    Layout.margins: 14
                    text: root.session ? "Nothing stored yet." : "No account is open."
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                }

                Repeater {
                    model: root.chats
                    delegate: RowLayout {
                        id: chatRow
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        spacing: 10
                        Avatar { fingerprint: chatRow.modelData.peer; size: 34 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: chatRow.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: chatRow.modelData.messages + " messages  ·  "
                                    + root.humanBytes(chatRow.modelData.bytes)
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                visible: chatRow.modelData.mediaCount > 0
                                text: chatRow.modelData.mediaCount
                                    + " pictures and voice notes are part of that"
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                        MenuButton {
                            text: "Trim…"
                            enabled: !root.busy
                            onClicked: {
                                trimDialog.peer = chatRow.modelData.peer
                                trimDialog.who = chatRow.modelData.name
                                trimDialog.open()
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.margins: 14
                    Layout.topMargin: 4
                    text: "These are what the messages and their pictures hold. The file on disk "
                        + "is larger: it also carries indexes, page overhead and free space. "
                        + "Trimming happens only on this device - your other devices keep their "
                        + "own copies, and nothing is asked of the people you talk to."
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Dialog {
        id: trimDialog
        property string peer: ""
        property string who: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(380, parent ? parent.width - 24 : 380)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label {
            text: "Trim " + trimDialog.who
            color: Theme.green
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: "Cancel"
            showReject: false
            onAccepted: trimDialog.close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: "Keep the newest messages and remove the rest, with their pictures and "
                    + "voice notes. This cannot be undone, and it happens only on this device."
            }
            MenuButton {
                Layout.fillWidth: true
                text: "Keep the last " + (root.session ? root.session.keepRecentMessages : 0)
                danger: true
                onClicked: trimDialog.run(root.session ? root.session.keepRecentMessages : 0)
            }
            MenuButton {
                Layout.fillWidth: true
                text: "Keep the last " + (root.session ? root.session.keepManyMessages : 0)
                danger: true
                onClicked: trimDialog.run(root.session ? root.session.keepManyMessages : 0)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: "Bazarish compacts the database afterwards so the space returns to the "
                    + "disk, and is busy while it does: a moment on an ordinary account, "
                    + "several seconds on one holding a hundred thousand messages."
            }
        }

        function run(keep) {
            if (!root.session) {
                return
            }
            if (trimDialog.peer.length > 0) {
                root.session.trimChat(trimDialog.peer, keep)
            } else {
                root.session.trimEveryChat(keep)
            }
            trimDialog.close()
        }
    }

    // Everything here holds the drawing thread for as long as it runs, so this is
    // painted first and stands until it is over. It carries no percentage: a
    // database rewrite reports none, and an invented one would be a lie about how
    // far along it is.
    Popup {
        id: working
        anchors.centerIn: Overlay.overlay
        modal: true
        visible: root.busy
        closePolicy: Popup.NoAutoClose
        padding: 20
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 6
            Label {
                text: (root.info.busyWhat !== undefined ? root.info.busyWhat : "Working") + "…"
                color: Theme.text
            }
            Label {
                text: "The window does not answer while this runs."
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
