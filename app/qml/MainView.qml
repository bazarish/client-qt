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
                ChatList {
                    id: chatList
                    session: root.session
                    Layout.preferredWidth: 320
                    Layout.fillHeight: true
                    onNewChatRequested: newChat.open()
                    onSettingsRequested: settings.open()
                    onAccountsRequested: accountSwitcher.open()
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: Theme.border }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Loader {
                        anchors.fill: parent
                        active: root.session && root.session.activePeer.length > 0
                        sourceComponent: ConversationView {
                            session: root.session
                            onContactInfoRequested: contactInfo.open()
                            onCallRequested: root.session.startCall("")
                        }
                    }
                    Label {
                        anchors.centerIn: parent
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
    ContactInfo { id: contactInfo; session: root.session }
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
        onShowAppSettings: appSettings.open()
    }

    // Unlock prompt for an encrypted account the user brings online/switches to.
    property string unlockId: ""
    Connections {
        target: App
        function onNeedPassphrase(id, name) {
            root.unlockId = id
            unlockField.text = ""
            unlockDialog.title = "Unlock " + name
            unlockDialog.open()
        }
    }
    Dialog {
        id: unlockDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        onAccepted: if (root.unlockId.length > 0) App.openProfile(root.unlockId, unlockField.text)
        background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
        header: Label { text: unlockDialog.title; color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; padding: 14; visible: text.length > 0 }
        footer: DialogButtons { acceptText: "Unlock"; onAccepted: unlockDialog.accept(); onRejected: unlockDialog.reject() }
        contentItem: TextField {
            id: unlockField
            echoMode: TextInput.Password
            placeholderText: "Passphrase"
            color: Theme.text
            placeholderTextColor: Theme.textDim
            implicitWidth: 280
            onAccepted: unlockDialog.accept()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: unlockField.activeFocus ? Theme.accent : Theme.border }
        }
    }
}
