// Bazarish project (c) 2026
import QtQuick
import Bazarish

Item {
    width: 400
    height: 600
    SessionStub {
        id: session
        objectName: "session"
        conversation: firstModel
    }
    ConversationView {
        objectName: "view"
        anchors.fill: parent
        session: session
    }
}
