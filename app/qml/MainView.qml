// Bazarish project (c) 2026
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

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onActionOk(info) { window.showToast(info) }
    }

    Column {
        id: strips
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        ConnectionStrip {
            width: strips.width
            session: root.session
            onTapped: routerStatus.open()
        }
        EpochReserveStrip {
            width: strips.width
            session: root.session
            onTapped: settings.open()
        }
    }
    CallBar {
        id: callBar
        anchors.top: strips.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        session: root.session
        minimized: root.callMinimized
        onExpandRequested: {
            if (App.callSession === root.session) {
                root.callMinimized = false
                callScreen.open()
            } else {
                App.switchTo(App.callSession.accountId)
            }
        }
    }
    Loader {
        id: pageLoader
        anchors.top: callBar.visible ? callBar.bottom : strips.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        sourceComponent: (root.session && root.session.connected) ? chatComponent : connectComponent
    }

    Connections {
        target: App
        function onInviteLinkOpened(link) { newChat.openInvite(link) }
        function onSignLinkOpened(link) {
            if (root.session && !root.session.primaryDevice) {
                window.showToast(qsTr("Only the primary device signs in by key"))
                return
            }
            signWithKeySheet.openWith(link)
        }
        function onServerLinkOpened(link) {
            if (root.session && root.session.connected) {
                window.showToast(qsTr("This account already has a server"))
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
                id: panes
                readonly property bool narrow: width < Theme.narrowWidth
                readonly property bool chatOpen: root.session
                    && root.session.activePeer.length > 0
                ChatList {
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
    InviteSheet { id: inviteSheet; session: root.session; onBack: { inviteSheet.close(); settings.open() } }
    SignWithKeySheet { id: signWithKeySheet; session: root.session; onBack: { signWithKeySheet.close(); settings.open() } }
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
    property bool callMinimized: false
    MicrophoneGate {
        id: callMicrophone
        onRefused: window.showToast(qsTr("No access to the microphone"))
    }
    CallScreen {
        id: callScreen
        session: root.session
        onMinimizeRequested: { root.callMinimized = true; callScreen.close() }
    }
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

    AccountSwitcher { id: accountSwitcher }
    RouterStatusPage { id: routerStatus; onBack: { routerStatus.close(); appSettings.open() } }
    StoragePage { id: storagePage; onBack: { storagePage.close(); settings.open() } }
    function offerGateway(link) { appSettings.offerGateway(link) }
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

    Connections {
        target: App
        function onNeedPassphrase(id, name) {
            unlockDialog.title = qsTr("Unlock %1").arg(name)
            unlockDialog.ask(id)
        }
        function onUnlockFailed(error) {
            unlockDialog.refused(error)
        }
        function onAccountUnlocked(id) {
            unlockDialog.unlocked()
        }
    }
    UnlockDialog {
        id: unlockDialog
        anchors.centerIn: Overlay.overlay
    }
}
