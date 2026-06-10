import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    objectName: "mainView"
    property var session: App.session

    // React to backend results with toasts.
    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onActionOk(info) { if (typeof window !== "undefined") window.showToast(info) }
        function onActionFailed(error) { if (typeof window !== "undefined") window.showToast(error) }
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
                            onCallRequested: callScreen.open()
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
    InviteSheet { id: inviteSheet; session: root.session }
    ContactInfo { id: contactInfo; session: root.session }
    CallScreen { id: callScreen; session: root.session }
    SettingsPage { id: settings; session: root.session; onShowInvite: inviteSheet.open() }

    Connections {
        target: newChat
        function onShowInvite() { inviteSheet.open() }
    }
}
