// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// An action in a call - answer, decline, hang up. One width for every one of
// them: a row of buttons that size themselves to their labels is a row that is
// never centred under the avatar.
Button {
    id: control

    property color fill: Theme.accent
    property color label: "white"

    readonly property int kWidth: 120
    readonly property int kHeight: 48

    Layout.preferredWidth: kWidth
    padding: 0
    hoverEnabled: true

    HoverHandler { enabled: control.enabled; cursorShape: Qt.PointingHandCursor }

    background: Rectangle {
        radius: control.kHeight / 2
        // A request in flight dims its button, so a press that is already being
        // carried out does not look like one that was ignored.
        color: !control.enabled ? Theme.surfaceAlt
            : control.down ? Qt.darker(control.fill, 1.2)
            : control.hovered ? Qt.lighter(control.fill, 1.15)
            : control.fill
        border.color: control.hovered && control.enabled ? Theme.text : Theme.border
        implicitWidth: control.kWidth
        implicitHeight: control.kHeight
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Label {
        text: control.text
        color: control.enabled ? control.label : Theme.textDim
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
