import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "mainView"
    property var session: App.session

    // Positive results stay as transient toasts; failures are routed to the
    // top-layer error dialog in Main.qml (so they are never covered or missed).
    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onActionOk(info) { if (typeof window !== "undefined") window.showToast(info) }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: (root.session && root.session.connected) ? chatComponent : connectComponent
    }

    Component {
        id: connectComponent
        ConnectServerPage { session: root.session }
    }

    Component {
        id: chatComponent
        Item {
            RowLayout {
                anchors.fill: parent
                spacing: 0
                // Below the narrow threshold this is the only pane until a chat
                // is opened, and the conversation replaces it rather than sharing
                // a width neither can use.
                id: panes
                readonly property bool narrow: width < Theme.narrowWidth
                readonly property bool chatOpen: root.session
                    && root.session.activePeer.length > 0
                ChatList {
                    id: chatList
                    session: root.session
                    visible: !panes.narrow || !panes.chatOpen
                    // Never wider than its share: the conversation used to be
                    // squeezed narrower than the list it sits beside.
                    Layout.preferredWidth: panes.narrow
                        ? panes.width
                        : Math.min(320, Math.round(panes.width * 0.38))
                    Layout.fillHeight: true
                    onNewChatRequested: newChat.open()
                    onSettingsRequested: settings.open()
                    onAppSettingsRequested: appSettings.open()
                    onAccountsRequested: accountSwitcher.open()
                }
                Rectangle {
                    visible: !panes.narrow
                    Layout.fillHeight: true
                    width: 1
                    color: Theme.border
                }
                Item {
                    id: chatPane
                    readonly property bool narrow: panes.narrow
                    visible: !panes.narrow || panes.chatOpen
                    clip: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Loader {
                        anchors.fill: parent
                        active: root.session && root.session.activePeer.length > 0
                        sourceComponent: ConversationView {
                            session: root.session
                            narrow: chatPane.narrow
                            onContactInfoRequested: contactInfo.open()
                            onCallRequested: root.session.startCall("")
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        // Bounded and wrapped: unbounded, it drew past its pane and
                        // over the list whenever the window left it little room.
                        width: Math.max(0, parent.width - 32)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        visible: !root.session || root.session.activePeer.length === 0
                        text: "Select a chat or start a new one"
                        color: Theme.textDim
                    }
                }
            }
        }
    }

    NewChatSheet { id: newChat; session: root.session }
    // These three open from Settings; their back button returns there.
    InviteSheet { id: inviteSheet; session: root.session; onBack: { inviteSheet.close(); settings.open() } }
    SignWithKeySheet { id: signWithKeySheet; session: root.session; onBack: { signWithKeySheet.close(); settings.open() } }
    ContactInfo {
        id: contactInfo
        session: root.session
        onShareRequested: contactShare.open()
    }
    ContactShareSheet {
        id: contactShare
        session: root.session
        onBack: { contactShare.close(); contactInfo.open() }
    }
    // A live call opens the full-screen overlay; the user can collapse it to the
    // compact banner below (root.callMinimized) and keep using the app.
    property bool callMinimized: false
    CallScreen {
        id: callScreen
        session: root.session
        onMinimizeRequested: { root.callMinimized = true; callScreen.close() }
    }
    // Open the overlay when a call becomes live (unless it was collapsed), and close
    // it - clearing the collapsed flag - when the call returns to idle.
    Connections {
        target: root.session
        function onCallChanged() {
            if (root.session.callState === "idle") {
                callScreen.close()
                root.callMinimized = false
            } else if (!root.callMinimized && !callScreen.opened) {
                callScreen.open()
            }
        }
    }

    // Minimized-call pill: a live call collapsed to a compact bar so the rest of the
    // app stays usable. Tap the text to return to the call; the x ends/cancels it.
    Rectangle {
        id: callBanner
        visible: root.session && root.session.callState !== "idle" && root.callMinimized
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 10
        z: 1000
        radius: 20
        height: 40
        width: bannerRow.implicitWidth + 24
        color: Theme.surface
        border.color: Theme.neon  // a live call: the rare neon highlight
        border.width: 1
        RowLayout {
            id: bannerRow
            anchors.centerIn: parent
            spacing: 8
            Label {
                Layout.leftMargin: 8
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                text: {
                    if (!root.session) return ""
                    var who = root.session.callPeerName
                    switch (root.session.callState) {
                    case "outgoing": return "Calling " + who + "…"
                    case "incoming": return "Incoming call · " + who
                    case "active": return "In call · " + who
                    }
                    return ""
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: { root.callMinimized = false; callScreen.open() } }
            }
            IconButton {
                visible: root.session
                    && (root.session.callState === "outgoing" || root.session.callState === "active")
                iconName: "close"
                onClicked: root.session.endCall()
            }
        }
    }
    AccountSwitcher { id: accountSwitcher }
    RouterStatusPage { id: routerStatus; onBack: { routerStatus.close(); appSettings.open() } }
    AppSettingsPage {
        id: appSettings
        onBack: { appSettings.close(); settings.open() }
        onShowRouterStatus: routerStatus.open()
    }
    SettingsPage {
        id: settings
        session: root.session
        onShowInvite: inviteSheet.open()
        onShowSignWithKey: signWithKeySheet.open()
        onShowRouterStatus: routerStatus.open()
    }

    // Unlock prompt for an encrypted account the user brings online/switches to.
    property string unlockId: ""
    Connections {
        target: App
        function onNeedPassphrase(id, name) {
            root.unlockId = id
            unlockField.text = ""
            unlockError.text = ""
            unlockDialog.title = "Unlock " + name
            unlockDialog.open()
        }
        // A wrong passphrase belongs here, on the screen where it was typed. The
        // prompt has not closed - it closes when the profile opens, or when the
        // user says Cancel - so the reason lands on it.
        function onUnlockFailed(error) {
            unlockError.text = error
            unlockField.text = ""
            unlockField.forceActiveFocus()
        }
        function onProfileUnlocked(id) {
            unlockError.text = ""
            unlockDialog.close()
        }
    }
    Dialog {
        id: unlockDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        closePolicy: Popup.CloseOnEscape
        // Submitting is not closing: Dialog.accept() would take the prompt away
        // before anyone knew whether the passphrase worked.
        function submit() {
            if (root.unlockId.length > 0) {
                App.openProfile(root.unlockId, unlockField.text)
            }
        }
        // Dismissed: whatever was waiting on it does not happen, and the account's
        // switch goes back to what is on disk.
        onRejected: App.cancelUnlock()
        // The one thing to do here is type a passphrase.
        onOpened: unlockField.forceActiveFocus()
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: unlockDialog.title; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14; visible: text.length > 0 }
        footer: DialogButtons { acceptText: "Unlock"; onAccepted: unlockDialog.submit(); onRejected: unlockDialog.reject() }
        contentItem: ColumnLayout {
            spacing: 6
            TextField {
                id: unlockField
                echoMode: TextInput.Password
                placeholderText: "Passphrase"
                color: Theme.text
                placeholderTextColor: Theme.textDim
                Layout.preferredWidth: 280
                onAccepted: unlockDialog.submit()
                background: Rectangle { radius: 8; color: Theme.surface; border.color: unlockField.activeFocus ? Theme.accent : Theme.border }
            }
            Label {
                id: unlockError
                visible: text.length > 0
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.preferredWidth: 280
            }
        }
    }
}
