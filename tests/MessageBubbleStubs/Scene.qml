// Bazarish project (c) 2026
import QtQuick
import Bazarish

Item {
    width: 400
    height: 600
    SessionStub {
        id: stubSession
        objectName: "session"
        conversation: rows
    }
    ConversationView {
        anchors.fill: parent
        session: stubSession
    }
}
