import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    padding: 18
    property string mode: "menu"
    property string errorText: ""
    // An alias to start from, set by openAlias; opening without one starts at
    // the menu as before.
    property string prefillAlias: ""
    // What an introduction says before anybody edits it. Named here because the
    // reset below has to put them back.
    readonly property string kInviteGreeting: "Hi, found your invite!"
    readonly property string kAliasGreeting: "Hi, add me?"
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        mode = root.prefillAlias.length > 0 ? "alias" : "menu"
        aliasField.text = root.prefillAlias
        errorText = ""
    }
    // Nothing typed here outlives the window: a link, a name and an introduction
    // are for one request, and the next one starts from a blank page.
    onClosed: {
        mode = "menu"
        errorText = ""
        inviteText.text = ""
        inviteIntro.text = root.kInviteGreeting
        aliasField.text = ""
        aliasIntro.text = root.kAliasGreeting
    }

    // Opens on the add-by-alias page with the alias filled in: the request is
    // still the user's to send.
    function openAlias(alias) {
        root.prefillAlias = alias
        root.open()
        root.prefillAlias = ""
    }

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    // The add-contact actions run on the worker thread (a lookup plus a sealed
    // delivery with retries - slow over I2P). The conversation the request belongs
    // to opens right away and the progress is written into it, so this sheet has
    // nothing left to wait for: it fires the request and closes.
    function startRequest(fn) { errorText = ""; fn(); close() }

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
            IconButton { iconName: "back"; font.pixelSize: 26; visible: root.mode !== "menu"; onClicked: root.mode = "menu" }
            Label { text: "New chat"; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

        Label {
            visible: root.errorText.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.danger
            text: root.errorText
        }

        // --- Menu ---
        ColumnLayout {
            visible: root.mode === "menu"
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [
                    { icon: "link", t: "Add by invite link", m: "invite" },
                    { icon: "bang", t: "Add by alias", m: "alias" }
                ]
                ItemDelegate {
                    id: menuItem
                    Layout.fillWidth: true
                    height: 48
                    text: modelData.t
                    hoverEnabled: true
                    onClicked: root.mode = modelData.m
                    // A solid surface row that lifts on hover (surfaceAlt + neon
                    // outline), so the choices stand out and react to the cursor.
                    contentItem: RowLayout {
                        spacing: 10
                        Icon { name: modelData.icon; color: Theme.textDim; size: 17; Layout.leftMargin: 10 }
                        Label {
                            text: menuItem.text
                            color: Theme.text
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                        }
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
            visible: root.mode === "invite"
            Layout.fillWidth: true
            spacing: 8
            Label { text: "Paste the bazarish:// invite link:"; color: Theme.textDim }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                TextArea { id: inviteText; wrapMode: TextArea.WrapAnywhere; color: Theme.text
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border } }
            }
            // Checked as it is typed: a paste that cannot work is refused here,
            // not by a background operation that dials I2P before finding out.
            Label {
                id: inviteCheck
                readonly property string problem: (root.session && inviteText.text.trim().length > 0)
                    ? root.session.inviteProblem(inviteText.text) : ""
                visible: problem.length > 0
                text: problem
                color: Theme.warn
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            // A contact request is admitted by nothing, and the protocol caps what
            // one may carry; the introduction is what is left over to write.
            FormField {
                id: inviteIntro
                label: "Introduction"
                text: root.kInviteGreeting
                maximumLength: App.maxGreetingLength
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "Send request"
                    enabled: inviteText.text.trim().length > 0 && inviteCheck.problem.length === 0
                    onClicked: root.startRequest(function() {
                        root.session.addByInvite(inviteText.text.trim(), inviteIntro.text)
                    })
                }
            }
        }

        // --- Add by alias ---
        ColumnLayout {
            visible: root.mode === "alias"
            Layout.fillWidth: true
            spacing: 8
            Label { text: "The resolver hands back the descriptor this alias stands for (it is trusted for that one mapping)."; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
            FormField { id: aliasField; label: "Alias" }
            FormField { id: aliasIntro; label: "Introduction"; text: root.kAliasGreeting }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "Send request"
                    enabled: aliasField.text.trim().length > 0
                    onClicked: root.startRequest(function() {
                        root.session.addByAlias(aliasField.text.trim(), aliasIntro.text)
                    })
                }
            }
        }

    }
}
