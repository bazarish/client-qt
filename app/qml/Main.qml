import QtQuick
import QtQuick.Controls
import Bazarish

ApplicationWindow {
    id: window
    visible: true
    width: 980
    height: 680
    minimumWidth: 360
    minimumHeight: 480
    title: "Bazarish"
    color: Theme.bg

    function showToast(message) {
        toastLabel.text = message
        toast.opacity = 1
        toastTimer.restart()
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: pickerComponent
    }

    Component { id: pickerComponent; ProfilePicker {} }
    Component { id: mainComponent; MainView {} }

    Connections {
        target: App
        function onProfileOpened() { stack.replace(null, mainComponent) }
        function onProfileOpenFailed(error) { window.showToast(error) }
        function onCreateFailed(error) { window.showToast(error) }
        function onSessionChanged() {
            if (!App.session && stack.depth > 0 && stack.currentItem
                    && stack.currentItem.objectName === "mainView") {
                stack.replace(null, pickerComponent)
            }
        }
    }

    // Lightweight toast for transient messages.
    Rectangle {
        id: toast
        opacity: 0
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 24
        radius: 10
        color: Theme.text
        width: Math.min(toastLabel.implicitWidth + 32, window.width - 48)
        height: toastLabel.implicitHeight + 20
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Label {
            id: toastLabel
            anchors.fill: parent
            anchors.margins: 10
            color: Theme.bg
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.opacity = 0 }
    }
}
