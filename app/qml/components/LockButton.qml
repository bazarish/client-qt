// Bazarish project (c) 2026
import QtQuick
import Bazarish

IconButton {
    visible: App.uiLockEnabled
    iconName: "lock"
    tint: Theme.danger
    Accessible.name: qsTr("Lock the app")
    onClicked: App.lockUi()
}
