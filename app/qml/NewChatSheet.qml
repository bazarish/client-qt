import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    signal showInvite()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 18
    property string mode: "menu"
    property bool busy: false
    property string errorText: ""
    property var selectedFps: []
    closePolicy: busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    onOpened: { mode = "menu"; busy = false; errorText = "" }

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // The add-contact actions run on the worker thread (a lookup plus a sealed
    // delivery with retries - slow over I2P), so reflect that immediately.
    function startRequest(fn) { errorText = ""; busy = true; fn() }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onActionOk(info) { if (root.busy) { root.busy = false; root.close() } }
        function onActionFailed(error) { if (root.busy) { root.busy = false; root.errorText = error } }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { text: "New chat"; color: Theme.neon; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { text: "✕"; visible: !root.busy; onClicked: root.close() }
        }

        // --- Sending (immediate feedback while the worker does the request) ---
        ColumnLayout {
            visible: root.busy
            Layout.fillWidth: true
            spacing: 12
            BusyIndicator { running: root.busy; Layout.alignment: Qt.AlignHCenter }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: "Sending request… this can take a moment while routing is\nresolved (longer over I2P)."
                color: Theme.textDim
            }
            Button { text: "Run in background"; Layout.alignment: Qt.AlignHCenter; onClicked: root.close() }
        }

        Label {
            visible: root.errorText.length > 0 && !root.busy
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.danger
            text: root.errorText
        }

        // --- Menu ---
        ColumnLayout {
            visible: !root.busy && root.mode === "menu"
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [
                    { t: "🔗  Add by invite link", m: "invite" },
                    { t: "@  Add by username", m: "username" },
                    { t: "#  Add by fingerprint", m: "fingerprint" },
                    { t: "👥  New group", m: "group" },
                    { t: "▣  Show my invite / QR", m: "showinvite" }
                ]
                ItemDelegate {
                    Layout.fillWidth: true
                    text: modelData.t
                    onClicked: {
                        if (modelData.m === "showinvite") { root.close(); root.showInvite() }
                        else if (modelData.m === "group") { root.selectedFps = []; root.mode = "group" }
                        else root.mode = modelData.m
                    }
                }
            }
        }

        // --- Add by invite ---
        ColumnLayout {
            visible: !root.busy && root.mode === "invite"
            Layout.fillWidth: true
            spacing: 8
            Label { text: "Paste the bazarish:// invite link:"; color: Theme.textDim }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                TextArea { id: inviteText; wrapMode: TextArea.WrapAnywhere; color: Theme.text
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border } }
            }
            FormField { id: inviteIntro; label: "Introduction"; text: "Hi, found your invite!" }
            RowLayout {
                Layout.fillWidth: true
                Button { text: "Back"; onClicked: root.mode = "menu" }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Send request"
                    enabled: inviteText.text.trim().length > 0
                    onClicked: root.startRequest(function() {
                        root.session.addByInvite(inviteText.text.trim(), inviteIntro.text)
                    })
                }
            }
        }

        // --- Add by username ---
        ColumnLayout {
            visible: !root.busy && root.mode === "username"
            Layout.fillWidth: true
            spacing: 8
            Label { text: "The resolver maps the name to a fingerprint (it is trusted for that mapping only)."; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
            FormField { id: usernameField; label: "Username (alias)" }
            FormField { id: usernameIntro; label: "Introduction"; text: "Hi, add me?" }
            RowLayout {
                Layout.fillWidth: true
                Button { text: "Back"; onClicked: root.mode = "menu" }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Send request"
                    enabled: usernameField.text.trim().length > 0
                    onClicked: root.startRequest(function() {
                        root.session.addByUsername(usernameField.text.trim(), usernameIntro.text)
                    })
                }
            }
        }

        // --- Add by fingerprint ---
        ColumnLayout {
            visible: !root.busy && root.mode === "fingerprint"
            Layout.fillWidth: true
            spacing: 8
            FormField { id: fpField; label: "Contact fingerprint (52 chars)" }
            FormField { id: fpIntro; label: "Introduction"; text: "Hi, add me?" }
            RowLayout {
                Layout.fillWidth: true
                Button { text: "Back"; onClicked: root.mode = "menu" }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Send request"
                    enabled: fpField.text.trim().length > 0
                    onClicked: root.startRequest(function() {
                        root.session.addByFingerprint(fpField.text.trim(), fpIntro.text)
                    })
                }
            }
        }

        // --- New group ---
        ColumnLayout {
            visible: !root.busy && root.mode === "group"
            Layout.fillWidth: true
            spacing: 8
            FormField { id: groupNameField; label: "Group name" }
            Label { text: "Pick members (existing contacts):"; color: Theme.textDim }
            Frame {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                background: Rectangle { color: Theme.surface; radius: 8; border.color: Theme.border }
                ListView {
                    id: memberList
                    anchors.fill: parent
                    clip: true
                    model: root.session ? root.session.contacts : null
                    delegate: CheckDelegate {
                        width: ListView.view.width
                        visible: !model.isGroup
                        height: model.isGroup ? 0 : 46
                        text: root.session ? root.session.shortFingerprint(model.fingerprint) : model.fingerprint
                        checked: root.selectedFps.indexOf(model.fingerprint) >= 0
                        onToggled: {
                            var arr = root.selectedFps.slice()
                            var i = arr.indexOf(model.fingerprint)
                            if (checked && i < 0) arr.push(model.fingerprint)
                            else if (!checked && i >= 0) arr.splice(i, 1)
                            root.selectedFps = arr
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button { text: "Back"; onClicked: root.mode = "menu" }
                Item { Layout.fillWidth: true }
                Label { text: root.selectedFps.length + " selected"; color: Theme.textDim }
                Button {
                    text: "Create"
                    enabled: groupNameField.text.trim().length > 0 && root.selectedFps.length > 0
                    onClicked: root.startRequest(function() {
                        root.session.createGroup(groupNameField.text.trim(), root.selectedFps)
                    })
                }
            }
        }
    }
}
