import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Shown right after a file is picked: the sender chooses how long the encrypted
// blob lives on the store. A TTL is always a backstop; an optional download
// count deletes the blob the moment that many recipients have fetched it
// (whichever comes first). Then it hands the choice to session.sendFile().
//
// Styled to the app's dialog language (Settings / New Chat): a dark surface with
// hairline borders, selectable chips that mark the choice with a neon outline,
// and the shared DialogButtons footer - no default-styled inputs.
Dialog {
    id: root
    property var session: null
    // The picked file (a file:// URL). Set this, then open().
    property url fileUrl: ""

    // Selected TTL (index into ttlOptions) and the download-count choice.
    property int ttlIndex: 2          // 1 week
    property bool limitDownloads: false

    readonly property string fileName: {
        const s = decodeURIComponent("" + root.fileUrl)
        const i = s.lastIndexOf("/")
        return i >= 0 ? s.substring(i + 1) : s
    }

    // TTL presets, in seconds. "1 week" matches the store's default TTL.
    readonly property var ttlOptions: [
        { label: "1 hour", secs: 3600 },
        { label: "1 day", secs: 86400 },
        { label: "1 week", secs: 604800 },
        { label: "30 days", secs: 2592000 }
    ]

    anchors.centerIn: Overlay.overlay
    modal: true
    width: 380
    padding: 0

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
    header: Label {
        text: "Send file"
        color: Theme.green
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Send"
        onAccepted: root.accept()
        onRejected: root.reject()
    }

    onAccepted: {
        if (root.session && ("" + root.fileUrl).length > 0) {
            const secs = root.ttlOptions[root.ttlIndex].secs
            const count = root.limitDownloads ? (parseInt(countField.text) || 1) : 0
            root.session.sendFile(root.fileUrl, secs, count)
        }
        root.fileUrl = ""
    }
    onRejected: root.fileUrl = ""

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            text: "📎  " + root.fileName
            color: Theme.text
            elide: Text.ElideMiddle
        }

        // TTL: a 2x2 grid of selectable chips (the active one carries a neon outline).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 6
            Label { text: "Auto-delete after:"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 8
                columnSpacing: 8
                Repeater {
                    model: root.ttlOptions
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        radius: Theme.radiusSmall
                        readonly property bool selected: root.ttlIndex === index
                        color: selected ? Theme.surfaceAlt : Theme.surface
                        border.color: selected ? Theme.neon : Theme.border
                        border.width: 1
                        Label {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: parent.selected ? Theme.text : Theme.textDim
                        }
                        TapHandler { onTapped: root.ttlIndex = index }
                    }
                }
            }
        }

        // Optional download cap: a Settings-style toggle plus a FormField-style count.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    text: "Delete after downloads"
                    color: Theme.text
                    verticalAlignment: Text.AlignVCenter
                }
                Switch {
                    checked: root.limitDownloads
                    onToggled: root.limitDownloads = checked
                }
            }
            RowLayout {
                visible: root.limitDownloads
                Layout.fillWidth: true
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    text: "Number of downloads:"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    verticalAlignment: Text.AlignVCenter
                }
                TextField {
                    id: countField
                    Layout.preferredWidth: 72
                    text: "1"
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.text
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 999 }
                    selectByMouse: true
                    background: Rectangle {
                        radius: 8
                        color: Theme.surface
                        border.color: countField.activeFocus ? Theme.accent : Theme.border
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: "The file is removed from the store once it has been downloaded "
                    + "this many times (each recipient counts once), or when the TTL "
                    + "above elapses - whichever comes first."
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }
}
