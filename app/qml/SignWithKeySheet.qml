import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Sign-in-with-key: paste a challenge from any Bazarish portal, see who it is
// for, and copy the signature back. Signing is not a step of its own - copying
// the signature is what asks for it - so the window is a field, what the
// challenge says about its consumer, and one button.
//
// No server is contacted, so it works even before a server is connected.
Popup {
    id: root
    property var session: null
    // Return to the page this opened from (Settings); the close button exits.
    signal back()
    // What the pasted challenge says about who will consume the signature. Read
    // again on every keystroke: it is what stands between the user and signing
    // in to somewhere they have never been.
    property var consumer: ({ "ok": false, "problem": "" })
    // Set while the signature this account was asked for is on its way back, so
    // the blob lands in the clipboard rather than in a box nobody reads.
    property bool copyPending: false

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    height: Math.min(parent ? parent.height - 40 : 600,
        headerRow.implicitHeight + body.implicitHeight + 32)
    padding: 0
    // The one thing to do here is paste a challenge, so the cursor is already in
    // the field that takes it.
    onOpened: challengeArea.forceActiveFocus()
    // A challenge names the place it lets its holder into, and it has no business
    // sitting in a closed window: what was pasted goes when the window does.
    onClosed: root.clear()
    function clear() {
        challengeArea.text = ""
        root.consumer = ({ "ok": false, "problem": "" })
        root.copyPending = false
        copyBtn.copied = false
    }

    background: DialogFrame { }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onLoginSigned(blob) {
            if (!root.copyPending) {
                return
            }
            root.copyPending = false
            App.copyText(blob)
            copyBtn.copied = true
            copiedTimer.restart()
        }
        // The account said no (a locked key, a challenge the core refuses): the
        // button goes back to asking rather than waiting for a blob that is not
        // coming. What went wrong is reported where every other failure is.
        function onActionFailed(message) { root.copyPending = false }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.margins: 14
            IconButton { iconName: "back"; onClicked: root.back() }
            Label {
                text: qsTr("Sign with the account key")
                color: Theme.green
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                id: body
                width: root.width
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 12

                    // Which identity will sign: the active account's name and fingerprint.
                    Rectangle {
                        visible: root.session && root.session.fingerprint.length > 0
                        Layout.fillWidth: true
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.color: Theme.border
                        implicitHeight: signAsCol.implicitHeight + 16
                        ColumnLayout {
                            id: signAsCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            Label {
                                text: root.session && root.session.displayName.length > 0
                                    ? qsTr("Signing as %1").arg(root.session.displayName)
                                    : qsTr("Signing as this account")
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

                    Label {
                        visible: !root.consumer.ok
                        text: qsTr("Challenge from the site")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                    ScrollView {
                        visible: !root.consumer.ok
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        // The challenge wraps, so the only scrolling that means
                        // anything here is downwards.
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        TextArea {
                            id: challengeArea
                            wrapMode: TextArea.WrapAnywhere
                            placeholderText: qsTr("Paste the challenge")
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                            selectByMouse: true
                            background: Rectangle { radius: 8; color: Theme.surface; border.color: challengeArea.activeFocus ? Theme.accent : Theme.border }
                            onTextChanged: {
                                copyBtn.copied = false
                                root.consumer = root.session
                                    ? root.session.describeLoginChallenge(text)
                                    : ({ "ok": false, "problem": "" })
                            }
                        }
                    }

                    // Who the signature is for. The signature is bound to this,
                    // and a place can only put its own words here, so comparing
                    // them with the site in front of you is the whole check.
                    Rectangle {
                        visible: challengeArea.text.trim().length > 0
                        Layout.fillWidth: true
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.color: root.consumer.ok ? Theme.green : Theme.danger
                        implicitHeight: consumerCol.implicitHeight + 16
                        ColumnLayout {
                            id: consumerCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            Label {
                                text: root.consumer.ok ? qsTr("You are signing in to")
                                    : qsTr("This challenge cannot be signed")
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                            Label {
                                visible: root.consumer.ok
                                text: root.consumer.name ? root.consumer.name : ""
                                color: Theme.text
                                font.pixelSize: Theme.fontTitle
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Repeater {
                                model: root.consumer.ok && root.consumer.place
                                    ? root.consumer.place : []
                                Label {
                                    text: modelData
                                    color: Theme.green
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                visible: root.consumer.ok
                                text: root.consumer.role ? qsTr("as %1").arg(root.consumer.role) : ""
                                color: Theme.textDim
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: root.consumer.ok
                                text: (root.consumer.place && root.consumer.place.length > 1)
                                    ? qsTr("If the address you came to is not in the list, do not sign.")
                                    : qsTr("Compare this with the site in front of you. If they differ, do not sign.")
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                            }
                            Label {
                                visible: !root.consumer.ok
                                text: root.consumer.problem ? root.consumer.problem : ""
                                color: Theme.danger
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        MenuButton {
                            visible: root.consumer.ok
                            iconName: "back"
                            text: qsTr("Back")
                            onClicked: {
                                root.clear()
                                challengeArea.forceActiveFocus()
                            }
                        }

                        Button {
                            id: copyBtn
                            hoverEnabled: true
                            property bool copied: false
                            Layout.fillWidth: true
                            text: copied ? qsTr("Copied") : qsTr("Copy signature")
                            enabled: root.session && root.consumer.ok && !root.copyPending
                            onClicked: {
                                // Belt as well as braces: `enabled` above already
                                // turns the button off while a signature is being
                                // made, but a press that arrives while this handler
                                // is still running would not have seen it.
                                if (root.copyPending) {
                                    return
                                }
                                root.copyPending = true
                                root.session.signLogin(challengeArea.text.trim())
                            }
                            background: Rectangle {
                                radius: 10
                                color: copyBtn.copied ? Theme.success
                                    : (copyBtn.enabled ? (copyBtn.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent)
                                        : Theme.surfaceAlt)
                                Behavior on color { ColorAnimation { duration: 200 } }
                            }
                            contentItem: IconLabel {
                                name: "copy"
                                color: copyBtn.enabled || copyBtn.copied ? Theme.accentText : Theme.textDim
                            }
                            Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
                        }
                    }
                }
            }
        }
    }
}
