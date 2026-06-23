import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

// Sign-in-with-key: paste a challenge from any Bazarish portal (or any site that
// supports it), sign it locally with this profile's key, and copy the signature
// back. No server is contacted, so it works even before a server is connected.
Popup {
    id: root
    property var session: null

    modal: true
    anchors.centerIn: Overlay.overlay
    // Cap at 480 but shrink with the window so the inputs and buttons (all
    // fillWidth) narrow on a small screen instead of overflowing.
    width: Math.min(480, (Overlay.overlay ? Overlay.overlay.width : 480) - 32)
    padding: 18
    onOpened: { challengeArea.text = ""; blobArea.text = "" }

    background: Rectangle { color: Theme.bg; radius: Theme.radius; border.color: Theme.border }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onLoginSigned(blob) { blobArea.text = blob }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Sign in with your key"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton { text: "✕"; onClicked: root.close() }
        }
        Label {
            text: "Your key is your sign-in for every Bazarish portal, and for any site that "
                + "supports sign-in-with-key. Paste the challenge the site shows; your key signs "
                + "it here and only the signature leaves. The key never goes to the site or a server."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Which identity will sign: the active profile's name and fingerprint.
        Rectangle {
            visible: root.session && root.session.fingerprint.length > 0
            Layout.fillWidth: true
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: Theme.border
            implicitHeight: signAsCol.implicitHeight + 16
            ColumnLayout {
                id: signAsCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Label {
                    text: root.session && root.session.displayName.length > 0
                        ? "Signing as " + root.session.displayName : "Signing as this profile"
                    color: Theme.text
                    font.weight: Font.Medium
                }
                Label {
                    text: root.session ? root.session.fingerprint : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }

        Label { text: "1. Challenge from the site"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            TextArea {
                id: challengeArea
                wrapMode: TextArea.WrapAnywhere
                placeholderText: "Paste the challenge"
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
            }
        }
        Button {
            Layout.fillWidth: true
            text: "Sign"
            enabled: root.session && challengeArea.text.trim().length > 0
            onClicked: root.session.signLogin(challengeArea.text.trim())
            background: Rectangle { radius: 10; color: parent.enabled ? Theme.accent : Theme.surfaceAlt }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }

        Label { text: "2. Paste this signature back into the site"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            TextArea {
                id: blobArea
                readOnly: true
                wrapMode: TextArea.WrapAnywhere
                placeholderText: "The signed blob appears here"
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
            }
        }
        Button {
            id: copyBtn
            property bool copied: false
            Layout.fillWidth: true
            text: copied ? "Copied ✓" : "Copy signature"
            enabled: blobArea.text.length > 0
            onClicked: {
                blobArea.selectAll(); blobArea.copy(); blobArea.deselect()
                copied = true; copiedTimer.restart()
            }
            background: Rectangle {
                radius: 10
                color: copyBtn.copied ? Theme.success : (copyBtn.enabled ? Theme.accent : Theme.surfaceAlt)
                Behavior on color { ColorAnimation { duration: 200 } }
            }
            contentItem: Label { text: copyBtn.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
            Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
        }
    }
}
