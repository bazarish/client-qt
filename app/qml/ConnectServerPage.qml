import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null

    // Reveal the per-field server inputs only after a link parses or the user
    // opts into manual entry; the link field alone is the default surface.
    property bool showManual: false
    // Animated border of the link field: flashes red on a bad paste, green on a
    // good one, then fades back - a hint without changing the screen.
    property color linkBorderColor: Theme.border

    function facadeList() {
        var urls = []
        for (var i = 0; i < facadeModel.count; ++i) {
            var u = facadeModel.get(i).url.trim()
            if (u.length > 0) urls.push(u)
        }
        return urls
    }

    function flashRed() {
        flashRevert.stop()
        root.linkBorderColor = Theme.danger
        flashRevert.interval = 2000
        flashRevert.start()
    }
    function flashGreen() {
        flashRevert.stop()
        root.linkBorderColor = Theme.success
        flashRevert.interval = 1000
        flashRevert.start()
    }

    // Parses the link field; on success fills the inputs and reveals them, on a
    // non-empty failure just flashes red (the screen is left untouched).
    function parseLink() {
        var t = linkField.text.trim()
        if (t.length === 0) {
            flashRevert.stop()
            root.linkBorderColor = Theme.border
            return
        }
        var info = root.session ? root.session.parseServerLink(t) : null
        if (info && info.serverFp && info.serverFp.length > 0) {
            facadeModel.clear()
            for (var i = 0; i < info.facades.length; ++i)
                facadeModel.append({ url: info.facades[i] })
            if (facadeModel.count === 0)
                facadeModel.append({ url: "" })
            fpField.text = info.serverFp
            root.showManual = true
            flashGreen()
        } else {
            flashRed()
        }
    }

    Timer { id: parseTimer; interval: 300; onTriggered: root.parseLink() }
    Timer { id: flashRevert; onTriggered: root.linkBorderColor = Theme.border }

    // Back to the profile list (no server needed to switch/create a profile).
    IconButton {
        text: "‹"
        font.pixelSize: 26
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        onClicked: App.requestAddAccount()
    }

    SignWithKeySheet { id: signSheet; session: root.session }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 480)
        spacing: 14

        Label {
            text: "Connect to a server"
            color: Theme.neon
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Your profile needs a serving server to send and receive. "
                + "Everything stays end-to-end encrypted; a facade is just the last mile."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Which profile is being connected: name and fingerprint.
        Rectangle {
            visible: root.session && root.session.fingerprint.length > 0
            Layout.fillWidth: true
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: Theme.border
            implicitHeight: idCol.implicitHeight + 16
            ColumnLayout {
                id: idCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Label {
                    text: root.session && root.session.displayName.length > 0
                        ? root.session.displayName : "This profile"
                    color: Theme.text
                    font.weight: Font.Medium
                }
                Label {
                    text: root.session ? root.session.fingerprint : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }

        // One-link import: paste a bazarish://server/... link; it is parsed
        // automatically and fills everything below.
        TextField {
            id: linkField
            Layout.fillWidth: true
            placeholderText: "Paste a bazarish://server/… link"
            color: Theme.text
            placeholderTextColor: Theme.textDim
            selectByMouse: true
            onTextChanged: parseTimer.restart()
            background: Rectangle {
                radius: 8
                color: Theme.surface
                border.color: Qt.colorEqual(root.linkBorderColor, Theme.border)
                    ? (linkField.activeFocus ? Theme.accent : Theme.border)
                    : root.linkBorderColor
                border.width: 1
                Behavior on border.color { ColorAnimation { duration: 300 } }
            }
        }

        // Opt into the per-field form when there is no link to paste.
        Label {
            visible: !root.showManual
            text: "Enter server details manually"
            color: Theme.accent
            font.pixelSize: Theme.fontSmall
            Layout.alignment: Qt.AlignHCenter
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.showManual = true
            }
        }

        // The per-field server inputs, hidden until a link parses or manual entry.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 14
            visible: root.showManual

            Label {
                text: "Facade URL(s) — tried in order, with failover:"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            ListModel {
                id: facadeModel
                ListElement { url: "" }
            }
            Repeater {
                model: facadeModel
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        Layout.fillWidth: true
                        text: model.url
                        placeholderText: "http[s]://host:port/secret-path"
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        selectByMouse: true
                        onTextChanged: facadeModel.setProperty(index, "url", text)
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: parent.activeFocus ? Theme.accent : Theme.border }
                    }
                    IconButton {
                        text: "✕"
                        visible: facadeModel.count > 1
                        onClicked: facadeModel.remove(index)
                    }
                }
            }
            Button {
                text: "＋ Add another facade"
                onClicked: facadeModel.append({ url: "" })
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                contentItem: Label { text: parent.text; color: Theme.accent; horizontalAlignment: Text.AlignHCenter }
            }

            FormField { id: fpField; label: "Server fingerprint" }

            Button {
                Layout.fillWidth: true
                text: "Connect & subscribe"
                enabled: root.facadeList().length > 0 && fpField.text.trim().length > 0
                onClicked: root.session.connectServer(root.facadeList(), fpField.text.trim())
                background: Rectangle { radius: 10; color: !parent.enabled ? Theme.surfaceAlt : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
                contentItem: Label { text: parent.text; color: parent.enabled ? Theme.accentText : Theme.textDim; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }

    // Footer: the key is the user's sign-in to every portal and any
    // sign-in-with-key site, available with or without a server.
    Button {
        text: "Signature"
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 18
        padding: 8
        onClicked: signSheet.open()
        background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
        contentItem: Label {
            text: parent.text; color: Theme.accent
            leftPadding: 14; rightPadding: 14
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
    }
}
