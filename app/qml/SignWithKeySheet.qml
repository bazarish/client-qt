import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Sign-in-with-key: paste a challenge from any Bazarish portal, see who it is
// for, and copy the signature back or send it. Signing is not a step of its own -
// copying or sending the signature is what asks for it - so the window is a field,
// what the challenge says about its consumer, and its buttons.
//
// The account's own server plays no part, so it works even before one is connected.
Popup {
    id: root
    bottomMargin: activeFocus ? App.keyboardInset : 0
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
    readonly property int kSendHoldSeconds: 5
    readonly property bool hasCallback: root.consumer.ok === true && !!root.consumer.callback
    // Send stays out of reach for a moment: long enough to read where the signature goes.
    property int holdLeft: 0
    // Pressed, and the core has not said so yet: a second press must not send twice.
    property bool asking: false
    readonly property bool sendingThis: root.hasCallback && root.session !== null
        && root.session.loginSendChallenge === challengeArea.text.trim()
    readonly property bool sendPending: root.asking || (root.sendingThis && root.session.loginSending)
    readonly property bool sent: root.sendingThis && root.session.loginAccepted
    readonly property string sendProblem: root.sendingThis && !root.session.loginSending
        && !root.session.loginAccepted ? root.session.loginSendStatus : ""
    property int sendElapsed: 0
    property int secondsLeft: 0
    readonly property bool expired: root.consumer.ok === true && root.secondsLeft <= 0
    readonly property int kSecondsPerMinute: 60
    readonly property int kClockDigits: 2

    function openWith(link) {
        root.open()
        challengeArea.text = link
    }

    modal: true
    focus: true
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
        root.asking = false
        root.holdLeft = 0
        holdTimer.stop()
    }

    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: root.opened && root.consumer.ok === true
        onTriggered: root.secondsLeft = Math.floor(root.consumer.expiresAt - Date.now() / 1000)
    }

    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: root.sendPending && root.opened
        onTriggered: root.sendElapsed = root.session && root.session.loginSendStartedAt > 0
            ? Math.max(0, Math.floor((Date.now() - root.session.loginSendStartedAt) / 1000)) : 0
    }

    Timer {
        id: holdTimer
        interval: 1000
        repeat: true
        onTriggered: {
            root.holdLeft -= 1
            if (root.holdLeft <= 0) {
                stop()
            }
        }
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
        function onLoginSendChanged() {
            if (root.sendingThis) {
                root.asking = false
            }
        }
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
                wrapMode: Text.Wrap
            }
            Label {
                visible: root.consumer.ok === true
                text: root.expired ? qsTr("Expired")
                    : Math.floor(root.secondsLeft / root.kSecondsPerMinute) + ":"
                        + String(root.secondsLeft % root.kSecondsPerMinute).padStart(root.kClockDigits, "0")
                color: root.expired ? Theme.danger : Theme.textDim
                font.pixelSize: Theme.fontSmall
                font.weight: root.expired ? Font.DemiBold : Font.Normal
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Hairline { }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            contentWidth: availableWidth
            clip: true
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
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        visible: !root.consumer.ok
                        Layout.fillWidth: true
                        spacing: 8
                        ScrollView {
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
                                    root.asking = false
                                    root.consumer = root.session
                                        ? root.session.describeLoginChallenge(text)
                                        : ({ "ok": false, "problem": "" })
                                    root.holdLeft = root.hasCallback ? root.kSendHoldSeconds : 0
                                    if (root.hasCallback) {
                                        holdTimer.restart()
                                    } else {
                                        holdTimer.stop()
                                    }
                                }
                            }
                        }
                        QrScanButton {
                            Layout.alignment: Qt.AlignTop
                            onDecoded: function(text) { challengeArea.text = text }
                        }
                    }

                    // Who the signature is for. The signature is bound to this,
                    // and a place can only put its own words here, so comparing
                    // them with where the challenge came from is the whole check.
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
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
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
                                    : qsTr("Compare this with where you got it. If they differ, do not sign.")
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
                            focusPolicy: Theme.pressFocus
                            hoverEnabled: true
                            property bool copied: false
                            Layout.fillWidth: !root.hasCallback
                            Layout.preferredWidth: root.hasCallback ? sendBtn.implicitHeight : -1
                            text: root.hasCallback ? "" : (copied ? qsTr("Copied") : qsTr("Copy signature"))
                            Accessible.name: qsTr("Copy signature")
                            ToolTip.visible: root.hasCallback && hovered
                            ToolTip.text: qsTr("Copy signature")
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
                                    : root.hasCallback ? (copyBtn.hovered ? Theme.surfaceAlt : Theme.surface)
                                    : (copyBtn.enabled ? (copyBtn.hovered ? Theme.accentHover : Theme.accent)
                                        : Theme.surfaceAlt)
                                border.color: root.hasCallback && !copyBtn.copied ? Theme.border : "transparent"
                                Behavior on color { ColorAnimation { duration: 200 } }
                            }
                            contentItem: IconLabel {
                                name: "copy"
                                color: copyBtn.copied ? Theme.accentText
                                    : !copyBtn.enabled ? Theme.textDim
                                    : root.hasCallback ? Theme.text : Theme.accentText
                            }
                            Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
                        }

                        Button {
                            id: sendBtn
                            focusPolicy: Theme.pressFocus
                            visible: root.hasCallback
                            hoverEnabled: true
                            Layout.fillWidth: true
                            text: root.sent ? qsTr("Sent")
                                : root.sendPending ? qsTr("Sending…")
                                : root.holdLeft > 0 ? qsTr("Send (%1)").arg(root.holdLeft)
                                : qsTr("Send")
                            enabled: root.session !== null && root.hasCallback && root.holdLeft === 0
                                && !root.sendPending && !root.sent
                            onClicked: {
                                root.asking = true
                                root.session.sendLogin(challengeArea.text.trim())
                            }
                            background: Rectangle {
                                radius: 10
                                color: root.sent ? Theme.success
                                    : (sendBtn.enabled ? (sendBtn.hovered ? Theme.accentHover : Theme.accent)
                                        : Theme.surfaceAlt)
                                Behavior on color { ColorAnimation { duration: 200 } }
                            }
                            contentItem: IconLabel {
                                name: "send"
                                color: sendBtn.enabled || root.sent ? Theme.accentText : Theme.textDim
                            }
                        }
                    }

                    ColumnLayout {
                        visible: root.sendPending
                        Layout.fillWidth: true
                        spacing: 6
                        ProgressBar {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 4
                            indeterminate: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: root.sendingThis ? root.session.loginSendStatus : ""
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("%1s").arg(root.sendElapsed)
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                    }

                    Label {
                        visible: root.sendProblem.length > 0
                        text: qsTr("Not sent: %1").arg(root.sendProblem)
                        color: Theme.danger
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
