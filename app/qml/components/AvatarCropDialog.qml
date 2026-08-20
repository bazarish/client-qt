// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Choosing which part of a picture becomes the avatar. The stock behaviour was
// a centre crop decided for the user, which cuts heads off portraits; here the
// picture is dragged and zoomed under a round window and what is inside it is
// what gets sent.
Dialog {
    id: root

    property var session: null
    // Where the cropped square is written before the session compresses it.
    readonly property string kOutputName: "avatar-crop.png"
    readonly property int kViewport: 240
    readonly property real kMaxZoom: 4.0
    readonly property int kOutputSize: 512

    signal cropped(string filePath)
    // Why the crop could not be handed over. Shown in the dialog itself: a write
    // that fails must not look like a picture that was accepted.
    property string errorText: ""

    function openFor(source) {
        errorText = ""
        picture.source = source
        zoom.value = 1.0
        picture.x = 0
        picture.y = 0
        open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(kViewport + 48, parent ? parent.width - 24 : kViewport + 48)
    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }
    header: Label {
        text: "Position your avatar"
        color: Theme.text
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: "Use avatar"
        onAccepted: root.commit()
        onRejected: root.reject()
    }

    // The keyboard path (Enter) goes through accept(), which closes first; commit
    // reopens the dialog if the crop could not be written.
    onAccepted: root.commit()

    // Grab exactly what the round window shows, at a size the compressor can work
    // from, and hand the file over. The target is an absolute path in a writable
    // place: a bare name lands next to wherever the app was started from, while
    // the name handed back used to point into the read-only bundle - so the
    // avatar was picked and then quietly dropped.
    function commit() {
        const target = App.scratchFile(kOutputName)
        viewport.grabToImage(function(result) {
            if (!result.saveToFile(target)) {
                root.errorText = "Could not write the cropped image to " + target
                root.open()
                return
            }
            root.errorText = ""
            root.cropped(target)
            root.close()
        }, Qt.size(root.kOutputSize, root.kOutputSize))
    }

    contentItem: ColumnLayout {
        spacing: 10

        Item {
            id: viewport
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: root.kViewport
            implicitHeight: root.kViewport
            clip: true

            Image {
                id: picture
                fillMode: Image.PreserveAspectFit
                // Filling the window at zoom 1 whichever way the picture is
                // oriented, so there is never a bare corner to send.
                width: implicitWidth >= implicitHeight
                    ? root.kViewport * zoom.value * (implicitWidth / Math.max(1, implicitHeight))
                    : root.kViewport * zoom.value
                height: implicitWidth >= implicitHeight
                    ? root.kViewport * zoom.value
                    : root.kViewport * zoom.value * (implicitHeight / Math.max(1, implicitWidth))
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                smooth: true
                DragHandler {
                    target: picture
                    xAxis.enabled: true
                    yAxis.enabled: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Icon { name: "search"; color: Theme.textDim; size: 14 }
            Slider {
                id: zoom
                Layout.fillWidth: true
                from: 1.0
                to: root.kMaxZoom
                value: 1.0
            }
        }

        Label {
            visible: root.errorText.length > 0
            Layout.fillWidth: true
            text: root.errorText
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
    }
}
