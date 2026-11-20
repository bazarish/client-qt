// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

ApplicationWindow {
    width: 400
    height: 600
    visible: true
    WindowPopup {
        objectName: "parentWindow"
        contentItem: Rectangle { implicitWidth: 200; implicitHeight: 200 }
    }
    WindowPopup {
        objectName: "childWindow"
        contentItem: Rectangle { implicitWidth: 100; implicitHeight: 100 }
    }
}
