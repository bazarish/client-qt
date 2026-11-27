// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    property bool turningOn: false
    property bool fingerprintReady: false
    property bool photoAsked: false

    onAboutToShow: root.fingerprintReady = App.mobile && App.fingerprintReady()

    function turn(on) {
        root.turningOn = on
        App.confirmOwner()
    }
    function askCamera() {
        if (cameraGate.granted) {
            App.setUiLockPhoto(true)
            return
        }
        root.photoAsked = true
        cameraGate.request()
    }
    Connections {
        target: App
        function onOwnerConfirmed(ok, problem) {
            if (!root.opened) {
                return
            }
            if (!ok) {
                if (problem.length > 0) {
                    window.showToast(problem)
                }
                return
            }
            if (root.turningOn) {
                pinSet.open()
            } else {
                App.disableUiLock()
            }
        }
    }
    CameraGate {
        id: cameraGate
        onStatusChanged: {
            if (!root.photoAsked || (!cameraGate.granted && !cameraGate.refused)) {
                return
            }
            root.photoAsked = false
            App.setUiLockPhoto(cameraGate.granted)
            if (!cameraGate.granted) {
                window.showToast(qsTr("No access to the camera"))
            }
        }
    }
    PinSetDialog {
        id: pinSet
        onChosen: (pin) => {
            App.enableUiLock(pin)
            if (App.mobile && App.uiLockPhoto) {
                root.askCamera()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("App lock")
            showBack: root.returnTo !== null
            onBackClicked: root.goBack()
            onCloseClicked: root.close()
        }

        ScrollColumn {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: Theme.radius
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.contentInset
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Enabled")
                        color: Theme.text
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Toggle {
                        checked: App.uiLockEnabled
                        onToggled: {
                            checked = Qt.binding(() => App.uiLockEnabled)
                            root.turn(!App.uiLockEnabled)
                        }
                    }
                }
                RowLayout {
                    visible: App.uiLockEnabled && root.fingerprintReady
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Unlock by fingerprint")
                        color: Theme.text
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Toggle {
                        checked: App.uiLockFingerprint
                        onToggled: App.setUiLockFingerprint(checked)
                    }
                }
                RowLayout {
                    visible: App.uiLockEnabled && App.mobile
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Take a photo")
                            color: Theme.text
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("After the third attempt with a wrong PIN, take a photo with the front camera")
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                    Toggle {
                        checked: App.uiLockPhoto
                        onToggled: {
                            checked = Qt.binding(() => App.uiLockPhoto)
                            if (App.uiLockPhoto) {
                                App.setUiLockPhoto(false)
                            } else {
                                root.askCamera()
                            }
                        }
                    }
                }
                RowLayout {
                    visible: App.uiLockEnabled
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Automatic lock")
                            color: Theme.text
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Lock the app after its window stays inactive for the set time")
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                    Dropdown {
                        id: autoLockBox
                        model: App.uiAutoLockChoices.map(minutes => ({
                            minutes: minutes,
                            label: Tr.language.length >= 0 ? App.autoLockLabel(minutes) : ""
                        }))
                        textRole: "label"
                        valueRole: "minutes"
                        Layout.preferredWidth: implicitWidth
                        Layout.alignment: Qt.AlignTop
                        function follow() { currentIndex = indexOfValue(App.uiAutoLockMinutes) }
                        onModelChanged: follow()
                        Component.onCompleted: follow()
                        onActivated: App.setUiAutoLock(currentValue)
                        Connections {
                            target: App
                            function onUiLockChanged() { autoLockBox.follow() }
                        }
                    }
                }
                RowLayout {
                    visible: App.uiLockEnabled
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Hide all notification text")
                            color: Theme.text
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("While the app is locked, a notification says only that a message came")
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                    Toggle {
                        checked: App.uiLockHidesAll
                        onToggled: App.setUiLockHidesAll(checked)
                    }
                }
            }
        }
    }
}
