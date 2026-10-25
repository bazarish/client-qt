import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "mainView"
    property var session: App.session
    // The open chat, set from inside the page component, whose ids this scope cannot see.
    property ConversationView conversation: null

    // On a narrow window an open chat covers the list, so back closes it.
    function goBack() {
        if (root.conversation && root.conversation.choosing) {
            root.conversation.clearChosen()
            return true
        }
        if (root.width < Theme.narrowWidth && root.session && root.session.activePeer.length > 0) {
            root.session.closeConversation()
            return true
        }
        return false
    }

    // Positive results stay as transient toasts; failures are routed to the
    // top-layer error dialog in Main.qml (so they are never covered or missed).
    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onActionOk(info) { if (typeof window !== "undefined") window.showToast(info) }
    }

    ConnectionStrip {
        id: connectionStrip
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        session: root.session
        onTapped: routerStatus.open()
    }
    Loader {
        id: pageLoader
        anchors.top: connectionStrip.visible ? connectionStrip.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        sourceComponent: (root.session && root.session.connected) ? chatComponent : connectComponent
    }

    Connections {
        target: App
        function onInviteLinkOpened(link) { newChat.openInvite(link) }
        function onSignLinkOpened(link) { signWithKeySheet.openWith(link) }
        function onServerLinkOpened(link) {
            if (root.session && root.session.connected) {
                window.showToast(qsTr("This account already has a server."))
                return
            }
            pageLoader.item.takeLink(link)
        }
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
                        onItemChanged: root.conversation = item
                        sourceComponent: ConversationView {
                            session: root.session
                            narrow: chatPane.narrow
                            onContactInfoRequested: contactInfo.open()
                            onCallRequested: callMicrophone.whenGranted(() => root.session.startCall(""))
                            onAddByNameRequested: function(alias) { newChat.openAlias(alias) }
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
                        text: qsTr("Select a chat or start a new one")
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
    // An accepted signature closes the sign-in window, so its outcome is told here.
    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onLoginSendChanged() {
            if (!App.attached || root.session.loginSending) {
                return
            }
            if (root.session.loginAccepted) {
                window.showToast(qsTr("The site accepted the signature"))
            } else if (!signWithKeySheet.opened) {
                window.showToast(qsTr("Not sent: %1").arg(root.session.loginSendStatus))
            }
        }
    }
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
    MicrophoneGate {
        id: callMicrophone
        onRefused: window.showToast(qsTr("No access to the microphone."))
    }
    CallScreen {
        id: callScreen
        session: root.session
        onMinimizeRequested: { root.callMinimized = true; callScreen.close() }
    }
    // Open the overlay when a call becomes live (unless it was collapsed), and close
    // it - clearing the collapsed flag - when the call returns to idle.
    function followCall() {
        if (!root.session) {
            return
        }
        if (root.session.callState === "idle") {
            callScreen.close()
            root.callMinimized = false
        } else if (!root.callMinimized && !callScreen.opened) {
            callScreen.open()
        }
    }
    // A window started by the call learns of it before this view exists.
    Component.onCompleted: followCall()
    onSessionChanged: followCall()
    Connections {
        target: root.session
        function onCallChanged() { root.followCall() }
    }

    Item {
        id: callBadge
        readonly property int kDiameter: 52
        readonly property int kMargin: 16
        readonly property int kIconSize: 24
        visible: !!root.session && root.session.callState !== "idle" && root.callMinimized
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: kMargin
        anchors.bottomMargin: Theme.barHeight + kMargin
        width: kDiameter
        height: kDiameter
        z: 1000
        readonly property string summary: {
            if (!root.session) return ""
            var who = root.session.callPeerName
            switch (root.session.callState) {
            case "outgoing": return qsTr("Calling %1…").arg(who)
            case "incoming": return qsTr("Incoming call · %1").arg(who)
            case "active": return qsTr("In call · %1").arg(who)
            }
            return ""
        }
        CallGlow {
            anchors.centerIn: parent
            avatarSize: callBadge.kDiameter
            level: !root.session ? 0
                : root.session.callState === "incoming" ? App.ringLevel
                : root.session.callState === "active"
                    ? Math.min(1, Theme.speechGain * Math.max(root.session.callOutputLevel,
                        root.session.callMuted ? 0 : root.session.callInputLevel))
                : 0
        }
        Button {
            id: callBadgeButton
            focusPolicy: Theme.pressFocus
            anchors.fill: parent
            padding: 0
            hoverEnabled: true
            Accessible.name: callBadge.summary
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            background: Rectangle {
                radius: width / 2
                color: callBadgeButton.down ? Qt.darker(Theme.green, 1.2)
                    : callBadgeButton.hovered ? Qt.lighter(Theme.green, 1.25) : Theme.green
                border.color: callBadgeButton.hovered ? Theme.text : Theme.border
                Behavior on color { ColorAnimation { duration: 90 } }
            }
            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    name: "call"
                    color: "white"
                    size: callBadge.kIconSize
                }
            }
            onClicked: { root.callMinimized = false; callScreen.open() }
        }
    }
    AccountSwitcher { id: accountSwitcher }
    RouterStatusPage { id: routerStatus; onBack: { routerStatus.close(); appSettings.open() } }
    StoragePage { id: storagePage; onBack: { storagePage.close(); settings.open() } }
    AppSettingsPage {
        id: appSettings
        onShowRouterStatus: routerStatus.open()
    }
    ConnectionLogPage {
        id: connectionLog
        session: root.session
        onBack: { connectionLog.close(); settings.open() }
    }
    SettingsPage {
        id: settings
        session: root.session
        onShowInvite: inviteSheet.open()
        onShowSignWithKey: signWithKeySheet.open()
        onShowStorage: storagePage.open()
        onShowRouterStatus: routerStatus.open()
        onShowConnectionLog: connectionLog.open()
    }

    // Unlock prompt for an encrypted account the user brings online/switches to.
    property string unlockId: ""
    Connections {
        target: App
        function onNeedPassphrase(id, name) {
            root.unlockId = id
            unlockField.text = ""
            unlockError.text = ""
            unlockDialog.title = qsTr("Unlock %1").arg(name)
            unlockDialog.open()
        }
        // A wrong passphrase belongs here, on the screen where it was typed. The
        // prompt has not closed - it closes when the account opens, or when the
        // user says Cancel - so the reason lands on it.
        function onUnlockFailed(error) {
            unlockError.text = error
            unlockField.text = ""
            unlockField.forceActiveFocus()
        }
        function onAccountUnlocked(id) {
            unlockError.text = ""
            unlockDialog.close()
        }
    }
    Dialog {
        id: unlockDialog
        bottomMargin: activeFocus ? App.keyboardInset : 0
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        // Submitting is not closing: Dialog.accept() would take the prompt away
        // before anyone knew whether the passphrase worked.
        function submit() {
            if (root.unlockId.length > 0) {
                App.openAccount(root.unlockId, unlockField.text)
            }
        }
        // Dismissed: whatever was waiting on it does not happen, and the account's
        // switch goes back to what is on disk.
        onRejected: App.cancelUnlock()
        // The one thing to do here is type a passphrase.
        onOpened: unlockField.forceActiveFocus()
        background: DialogFrame { }
        header: Label { text: unlockDialog.title; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14; visible: text.length > 0 }
        footer: DialogButtons { acceptText: qsTr("Unlock"); onAccepted: unlockDialog.submit(); onRejected: unlockDialog.reject() }
        contentItem: ColumnLayout {
            spacing: 6
            TextField {
                id: unlockField
                echoMode: TextInput.Password
                placeholderText: qsTr("Passphrase")
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
