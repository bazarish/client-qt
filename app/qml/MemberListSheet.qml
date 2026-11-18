// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

WindowPopup {
    id: root
    bottomMargin: activeFocus ? App.keyboardInset : 0
    property var session: null
    property bool searching: false
    readonly property int kRowHeight: 44
    readonly property int kLetterSize: 30
    readonly property var members: root.session
        ? root.session.membersInOrder(root.session.activeMembers, Tr.language) : []
    readonly property var shown: root.session && root.searching
        ? root.session.membersMatching(root.members, filter.displayText) : root.members

    function stopSearch() {
        root.searching = false
        filter.text = ""
    }

    onAboutToShow: {
        root.stopSearch()
        list.positionViewAtBeginning()
    }

    contentItem: ColumnLayout {
        spacing: 0

        WindowHeader {
            title: qsTr("Member list")
            onCloseClicked: root.close()
            Label {
                text: root.members.length
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            IconButton {
                iconName: "search"
                tint: root.searching ? Theme.green : Theme.text
                Accessible.name: qsTr("Search members")
                onClicked: {
                    if (root.searching) {
                        root.stopSearch()
                    } else {
                        root.searching = true
                        filter.forceActiveFocus()
                    }
                }
            }
        }

        TextField {
            id: filter
            visible: root.searching
            Layout.fillWidth: true
            Layout.margins: Theme.contentInset
            placeholderText: qsTr("Search members")
            color: Theme.text
            placeholderTextColor: Theme.textDim
            selectByMouse: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: contentHeight
            clip: true
            topMargin: Theme.rowInset
            bottomMargin: Theme.radius
            model: root.shown
            ScrollBar.vertical: ScrollBar { }
            delegate: Item {
                id: memberRow
                required property string modelData
                width: ListView.view.width
                height: root.kRowHeight
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.contentInset
                    anchors.rightMargin: Theme.contentInset
                    spacing: 10
                    Rectangle {
                        Layout.preferredWidth: root.kLetterSize
                        Layout.preferredHeight: root.kLetterSize
                        radius: width / 2
                        color: root.session ? root.session.letterFill(memberRow.modelData) : Theme.surfaceAlt
                        Label {
                            anchors.centerIn: parent
                            text: root.session ? root.session.letterOf(memberRow.modelData) : ""
                            color: root.session
                                ? root.session.letterInk(memberRow.modelData) : Theme.text
                            font.pixelSize: Theme.fontSmall + 1
                            font.weight: Font.DemiBold
                        }
                    }
                    Label {
                        text: memberRow.modelData
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            Label {
                visible: list.count === 0 && root.searching
                x: Theme.contentInset
                y: Theme.contentInset
                width: list.width - 2 * Theme.contentInset
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("No member by that name")
                color: Theme.textDim
            }
        }
    }
}
