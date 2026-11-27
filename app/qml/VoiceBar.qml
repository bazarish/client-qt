// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Rectangle {
    id: root
    property var session: null
    property bool noteInSight: false
    readonly property int kPadding: 8
    readonly property int kSideMargin: 12
    readonly property int kAvatarSize: 36

    visible: !!root.session && root.session.voicePlaying.length > 0 && !root.noteInSight
    implicitHeight: row.implicitHeight + 2 * root.kPadding
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: root.kSideMargin
        anchors.rightMargin: root.kSideMargin / 2
        spacing: 10

        Avatar {
            fingerprint: root.session ? root.session.voicePlayingPeer : ""
            size: root.kAvatarSize
            Layout.alignment: Qt.AlignVCenter
        }
        VoiceTrack {
            Layout.fillWidth: true
            session: root.session
            e2eId: root.session ? root.session.voicePlaying : ""
            durationMs: root.session ? root.session.voicePlayingDurationMs : 0
            wave: root.session ? root.session.voicePlayingWave : ""
        }
        IconButton {
            iconName: "close"
            Accessible.name: qsTr("Stop playback")
            onClicked: root.session.stopVoice()
        }
    }
}
