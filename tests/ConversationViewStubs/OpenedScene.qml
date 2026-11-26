// Bazarish project (c) 2026
import QtQuick
import Bazarish

Item {
    width: 400
    height: 600
    SessionStub {
        id: session
        objectName: "session"
        activePeer: "a"
        conversation: firstModel
    }
    ConversationView {
        anchors.fill: parent
        session: session
    }
}
