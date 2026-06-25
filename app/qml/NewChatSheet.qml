import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null

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

    // Primary action button: near-white accent fill, darker on hover/press, so it
    // reads clearly against the dark popup (the default Basic Button blends in).
    component ActionButton: Button {
        id: ctl
        hoverEnabled: true
        background: Rectangle {
            radius: 10
            color: !ctl.enabled ? Theme.surfaceAlt
                : (ctl.down ? Qt.darker(Theme.accent, 1.2)
                : (ctl.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent))
        }
        contentItem: Label {
            text: ctl.text
            color: ctl.enabled ? Theme.accentText : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            leftPadding: 14; rightPadding: 14
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            // Back to the menu page, shown left of the title while on a sub-page.
            IconButton { text: "‹"; font.pixelSize: 26; visible: !root.busy && root.mode !== "menu"; onClicked: root.mode = "menu" }
            Label { text: "New chat"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
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
                text: "Sending request… this can take a moment while routing is resolved."
                color: Theme.textDim
            }
            // When the embedded I2P router is off, routing falls back to the
            // server proxy - say so, so the wait is explained rather than silent.
            Label {
                visible: !I2p.enabled
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: "I2P is off — relaying through your server."
                color: Theme.warn
                font.pixelSize: Theme.fontSmall
            }
            Button {
                id: bgBtn
                text: "Run in background"
                hoverEnabled: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: root.close()
                background: Rectangle {
                    radius: 10
                    color: bgBtn.down ? Theme.border2 : (bgBtn.hovered ? Theme.surfaceAlt : Theme.surface)
                    border.color: bgBtn.hovered ? Theme.green : Theme.border
                    border.width: 1
                }
                contentItem: Label { text: bgBtn.text; color: Theme.text; horizontalAlignment: Text.AlignHCenter; leftPadding: 14; rightPadding: 14 }
            }
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
                    { t: "👥  New group", m: "group" }
                ]
                ItemDelegate {
                    id: menuItem
                    Layout.fillWidth: true
                    height: 48
                    text: modelData.t
                    hoverEnabled: true
                    onClicked: {
                        if (modelData.m === "group") { root.selectedFps = []; root.mode = "group" }
                        else root.mode = modelData.m
                    }
                    // A solid surface row that lifts on hover (surfaceAlt + neon
                    // outline), so the choices stand out and react to the cursor.
                    contentItem: Label {
                        text: menuItem.text
                        color: Theme.text
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: menuItem.down ? Theme.border2
                            : (menuItem.hovered ? Theme.surfaceAlt : Theme.surface)
                        border.color: menuItem.hovered ? Theme.green : Theme.border
                        border.width: 1
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
                Item { Layout.fillWidth: true }
                ActionButton {
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
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "Send request"
                    enabled: usernameField.text.trim().length > 0
                    onClicked: root.startRequest(function() {
                        root.session.addByUsername(usernameField.text.trim(), usernameIntro.text)
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
                Item { Layout.fillWidth: true }
                Label { text: root.selectedFps.length + " selected"; color: Theme.textDim }
                ActionButton {
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
