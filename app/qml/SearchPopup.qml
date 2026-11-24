// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: popup
    property var session: null
    signal jumpRequested(var localId)

    property var results: []

    parent: Overlay.overlay
    modal: true
    focus: true
    x: parent ? (parent.width - width) / 2 : 0
    y: Theme.windowTopRoom / 2
    width: Math.min(Theme.windowWidth, parent ? parent.width - Theme.windowSideRoom : Theme.windowWidth)
    height: parent ? parent.height - Theme.windowTopRoom - (activeFocus ? App.keyboardInset : 0)
        : implicitHeight
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: DialogFrame { }

    function openSearch() {
        results = []
        field.text = ""
        open()
        field.forceActiveFocus()
    }
    function runSearch() {
        // A keyboard holds the word being typed outside text until it commits it.
        results = (session && field.displayText.length > 0)
            ? session.searchMessages(field.displayText) : []
    }

    Timer { id: searchDebounce; interval: 200; onTriggered: popup.runSearch() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: field
                Layout.fillWidth: true
                placeholderText: qsTr("Search in conversation...")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                selectByMouse: true
                onDisplayTextChanged: searchDebounce.restart()
                background: Rectangle {
                    radius: 8
                    color: Theme.bg
                    border.color: field.activeFocus ? Theme.accent : Theme.controlEdge
                }
            }
            IconButton { iconName: "close"; onClicked: popup.close() }
        }

        Label {
            Layout.fillWidth: true
            visible: field.displayText.length > 0
            text: popup.results.length === 0
                ? qsTr("No matches")
                : qsTr("Matches: %1").arg(popup.results.length)
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: popup.results
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: hitDelegate
                width: list.width
                onClicked: popup.jumpRequested(modelData.id)
                background: Rectangle {
                    radius: 6
                    color: hitDelegate.hovered ? Theme.bg : "transparent"
                }
                contentItem: ColumnLayout {
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: modelData.author
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Label {
                            text: modelData.time
                                ? new Date(modelData.time).toLocaleString(
                                    Qt.locale(Tr.language), "dd MMM hh:mm")
                                : ""
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: App.markupPlain(modelData.text)
                        color: Theme.text
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
