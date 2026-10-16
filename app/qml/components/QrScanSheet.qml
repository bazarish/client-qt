// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import Bazarish

CameraSheet {
    id: sheet
    signal decoded(string text)

    property string errorText: ""
    readonly property int kBarMargin: 20

    onOpened: sheet.errorText = ""
    onCameraFailed: function(reason) { sheet.errorText = reason }

    QrScanner {
        sink: sheet.preview ? sheet.preview.videoSink : null
        onDecoded: function(text) {
            sheet.close()
            sheet.decoded(text)
        }
    }
    Label {
        visible: sheet.errorText.length > 0
        text: sheet.errorText
        color: Theme.danger
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: sheet.kBarMargin
    }
}
