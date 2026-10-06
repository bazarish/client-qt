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
    readonly property int kViewport: 240
    readonly property real kMaxZoom: 4.0
    readonly property int kOutputSize: 512

    signal cropped(var grab)

    function openFor(source) {
        picture.source = source
        zoom.value = 1.0
        picture.x = 0
        picture.y = 0
        open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(kViewport + 48, parent ? parent.width - 24 : kViewport + 48)
    background: DialogFrame { }
    header: Label {
        text: qsTr("Position your avatar")
        color: Theme.text
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        padding: 14
    }
    footer: DialogButtons {
        acceptText: qsTr("Use avatar")
        onAccepted: root.commit()
        onRejected: root.reject()
    }

    // The keyboard path (Enter) goes through accept(), which closes first; commit
    // reopens the dialog if the crop could not be written.
    onAccepted: root.commit()

    function commit() {
        viewport.grabToImage(function(result) {
            root.cropped(result)
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

            // The round window this dialog has always talked about, now drawn: a
            // scrim over the square with a circular hole in it, so what is chosen
            // is what will be seen. The grab underneath stays square - the picture
            // is stored as it was cropped, and the circle is how it is shown.
            Canvas {
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.72)
                    ctx.fillRect(0, 0, width, height)
                    ctx.globalCompositeOperation = "destination-out"
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, Math.min(width, height) / 2, 0, 2 * Math.PI)
                    ctx.fill()
                    ctx.globalCompositeOperation = "source-over"
                    ctx.strokeStyle = Theme.border2
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, Math.min(width, height) / 2 - 0.5,
                        0, 2 * Math.PI)
                    ctx.stroke()
                }
                // Nothing here reacts to the pointer: the picture underneath is
                // what is dragged.
                enabled: false
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
    }
}
