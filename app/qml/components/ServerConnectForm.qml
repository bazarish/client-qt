// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// The server descriptor entry, shared by the first-connect page and the Settings
// connection editor so both offer the same flow. Accepts a bazarish://server/...
// link (parsed into the fields) or manual entry: an ordered facade list (tried
// with failover) and the server fingerprint. The action calls connectServer on
// the session, which binds the endpoint and registers this client; the user
// registers separately on the server's own portal (there is no automatic
// registration here). Re-running it after a portal registration finishes setup.
ColumnLayout {
    id: form
    property var session: null
    // Label of the primary action button.
    property string actionText: "Connect"
    // Prefill, used when editing an existing connection from Settings.
    property var initialFacades: []
    property string initialFingerprint: ""
    // Reveal the per-field inputs straight away (true when editing) or keep the
    // link field as the only surface until a link parses or manual entry is asked.
    property bool showManual: false
    // Where a router with no peers can ask for a slice of netdb: taken from the
    // pasted descriptor, never typed by hand - a user has no way to know them.
    property var reseedUrls: []
    // Emitted right after the action runs, so a host dialog can close.
    signal submitted()

    // Animated border of the link field: red on a bad paste, green on a good one.
    property color linkBorderColor: Theme.border

    spacing: 14
    Layout.fillWidth: true

    Component.onCompleted: reset()

    // (Re)loads the fields from initialFacades/initialFingerprint. The host calls
    // this when reopening the editor so it always reflects the current endpoint.
    function reset() {
        facadeModel.clear()
        for (var i = 0; i < initialFacades.length; ++i) {
            facadeModel.append({ url: initialFacades[i] })
        }
        if (facadeModel.count === 0) {
            facadeModel.append({ url: "" })
        }
        fpField.text = initialFingerprint
        linkField.text = ""
        form.showManual = initialFingerprint.length > 0
        flashRevert.stop()
        form.linkBorderColor = Theme.border
    }

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
        form.linkBorderColor = Theme.danger
        flashRevert.interval = 2000
        flashRevert.start()
    }
    function flashGreen() {
        flashRevert.stop()
        form.linkBorderColor = Theme.success
        flashRevert.interval = 1000
        flashRevert.start()
    }

    // Parses the link field; on success fills the inputs and reveals them, on a
    // non-empty failure just flashes red (the fields are left untouched).
    function parseLink() {
        var t = linkField.text.trim()
        if (t.length === 0) {
            flashRevert.stop()
            form.linkBorderColor = Theme.border
            return
        }
        var info = form.session ? form.session.parseServerLink(t) : null
        if (info && info.serverFp && info.serverFp.length > 0) {
            form.reseedUrls = info.reseeds || []
            facadeModel.clear()
            for (var i = 0; i < info.facades.length; ++i) {
                facadeModel.append({ url: info.facades[i] })
            }
            if (facadeModel.count === 0) {
                facadeModel.append({ url: "" })
            }
            fpField.text = info.serverFp
            form.showManual = true
            flashGreen()
        } else {
            flashRed()
        }
    }

    Timer { id: parseTimer; interval: 300; onTriggered: form.parseLink() }
    Timer { id: flashRevert; onTriggered: form.linkBorderColor = Theme.border }

    ListModel { id: facadeModel }

    // One-link import: paste a bazarish://server/... link; it parses automatically
    // and fills everything below.
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
            border.color: Qt.colorEqual(form.linkBorderColor, Theme.border)
                ? (linkField.activeFocus ? Theme.accent : Theme.border)
                : form.linkBorderColor
            border.width: 1
            Behavior on border.color { ColorAnimation { duration: 300 } }
        }
    }

    // Opt into the per-field form when there is no link to paste.
    Label {
        visible: !form.showManual
        text: "Enter server details manually"
        color: Theme.accent
        font.pixelSize: Theme.fontSmall
        font.underline: manualMa.containsMouse
        Layout.alignment: Qt.AlignHCenter
        MouseArea {
            id: manualMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: form.showManual = true
        }
    }

    // The per-field inputs, hidden until a link parses or manual entry is chosen.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14
        visible: form.showManual

        Label {
            text: "Facade URL(s) — tried in order, with failover:"
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
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
                    iconName: "close"
                    visible: facadeModel.count > 1
                    onClicked: facadeModel.remove(index)
                }
            }
        }
        MenuButton {
            Layout.fillWidth: true
            text: "Add another facade"
            onClicked: facadeModel.append({ url: "" })
        }

        // What the descriptor brought besides the facades. A reseed is a clearnet
        // address this client will fetch from before it has any I2P at all, so it
        // is the one thing in a pasted link worth reading before agreeing to it -
        // and it used to be applied without ever being shown.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: form.reseedUrls.length > 0

            Label {
                text: form.reseedUrls.length === 1
                    ? "Reseed from the link — fetched over clearnet, before I2P is up:"
                    : "Reseeds from the link — fetched over clearnet, before I2P is up:"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Repeater {
                model: form.reseedUrls
                Label {
                    text: modelData
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }

        FormField { id: fpField; label: "Server fingerprint" }

        Button {
            Layout.fillWidth: true
            text: (form.session && form.session.connecting) ? "Connecting…" : form.actionText
            hoverEnabled: true
            enabled: form.session && !form.session.connecting
                && form.facadeList().length > 0 && fpField.text.trim().length > 0
            onClicked: {
                form.session.connectServer(form.facadeList(), fpField.text.trim(),
                    form.reseedUrls)
                form.submitted()
            }
            background: Rectangle { radius: 10; color: !parent.enabled ? Theme.surfaceAlt : (parent.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent) }
            contentItem: Label { text: parent.text; color: parent.enabled ? Theme.accentText : Theme.textDim; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
