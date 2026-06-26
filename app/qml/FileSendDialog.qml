import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Shown right after a file is picked: the sender chooses how long the encrypted
// blob lives on the store. A TTL is always a backstop; an optional download
// count deletes the blob the moment that many recipients have fetched it
// (whichever comes first). Then it hands the choice to session.sendFile().
Dialog {
    id: root
    property var session: null
    // The picked file (a file:// URL). Set this, then open().
    property url fileUrl: ""

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
            const secs = root.ttlOptions[ttlBox.currentIndex].secs
            root.session.sendFile(root.fileUrl, secs, countCheck.checked ? countSpin.value : 0)
        }
        root.fileUrl = ""
    }
    onRejected: root.fileUrl = ""

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 4
            text: "📎  " + root.fileName
            color: Theme.text
            elide: Text.ElideMiddle
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            spacing: 4
            Label { text: "Auto-delete after:"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            ComboBox {
                id: ttlBox
                Layout.fillWidth: true
                currentIndex: 2  // 1 week
                model: root.ttlOptions.map(function(o) { return o.label })
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 8
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                CheckBox { id: countCheck; text: "Delete after downloads" }
                Item { Layout.fillWidth: true }
                SpinBox {
                    id: countSpin
                    enabled: countCheck.checked
                    from: 1
                    to: 999
                    value: 1
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
