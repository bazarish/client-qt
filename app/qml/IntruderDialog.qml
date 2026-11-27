// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Dialog {
    id: root
    readonly property int kPhotoHeight: 260

    function moment(ms) {
        return Qt.formatDateTime(new Date(ms), "yyyy-MM-dd HH:mm")
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(Theme.dialogWidth, parent ? parent.width - Theme.windowSideRoom : Theme.dialogWidth)
    padding: 0

    background: DialogFrame { destructive: true }
    header: WindowHeader {
        title: qsTr("Unauthorized access")
        onCloseClicked: App.intruderSeen()
    }
    footer: DialogButtons {
        acceptText: qsTr("Got it")
        showReject: false
        onAccepted: App.intruderSeen()
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            text: qsTr("Wrong PIN entries: %1, the last one %2")
                .arg(App.intruderCount).arg(root.moment(App.intruderLastMs))
            color: Theme.text
            wrapMode: Text.Wrap
        }
        Image {
            visible: App.intruderPhoto.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 12
            Layout.preferredHeight: root.kPhotoHeight
            source: App.intruderPhoto
            fillMode: Image.PreserveAspectFit
            cache: false
            asynchronous: true
        }
    }
}
