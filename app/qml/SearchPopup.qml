import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// In-conversation full-text search. Lists matching messages (newest first);
// clicking one asks the conversation to jump to it (and closes the popup).
Popup {
    id: popup
    property var session: null
    signal jumpRequested(var localId)

    property var results: []

    modal: true
    focus: true
    width: Math.min(parent ? parent.width * 0.82 : 480, 520)
    height: Math.min(parent ? parent.height * 0.82 : 600, 640)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.surface; radius: 10; border.color: Theme.border }

    function openSearch() {
        results = []
        field.text = ""
        open()
        field.forceActiveFocus()
    }
    function runSearch() {
        results = (session && field.text.length > 0) ? session.searchMessages(field.text) : []
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
                placeholderText: qsTr("Search in conversation…")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                selectByMouse: true
                onTextChanged: searchDebounce.restart()
                background: Rectangle {
                    radius: 8
                    color: Theme.bg
                    border.color: field.activeFocus ? Theme.accent : Theme.border
                }
            }
            IconButton { iconName: "close"; onClicked: popup.close() }
        }

        Label {
            Layout.fillWidth: true
            visible: field.text.length > 0
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
