import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Bazarish

Popup {
    id: root
    property var session: null

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    padding: 18
    property string errorText: ""
    property string prefillAlias: ""
    property bool scanning: false
    readonly property string kGreeting: "Hi, add me?"

    readonly property string typed: targetText.text.trim()
    readonly property bool byAlias: root.typed.startsWith(App.aliasSigil)
    readonly property string problem: {
        if (!root.session || root.typed.length === 0) {
            return ""
        }
        return root.byAlias ? root.session.aliasProblem(root.typed)
                            : root.session.inviteProblem(root.typed)
    }

    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        targetText.text = root.prefillAlias
        errorText = ""
        scanning = false
    }
    onClosed: {
        errorText = ""
        scanning = false
        targetText.text = ""
        intro.text = root.kGreeting
    }

    function openAlias(alias) {
        root.prefillAlias = App.aliasSigil + alias
        root.open()
        root.prefillAlias = ""
    }

    background: DialogFrame { }

    // The add-contact actions run on the worker thread (a lookup plus a sealed
    // delivery with retries - slow over I2P). The conversation the request belongs
    // to opens right away and the progress is written into it, so this sheet has
    // nothing left to wait for: it fires the request and closes.
    function startRequest(fn) { errorText = ""; fn(); close() }

    // Primary action button: near-white accent fill, darker on hover/press, so it
    // reads clearly against the dark popup (the default Basic Button blends in).
    component ActionButton: Button {
        id: ctl
        hoverEnabled: true
        background: Rectangle {
            radius: 10
            color: !ctl.enabled ? Theme.surfaceAlt
                : (ctl.down ? Qt.darker(Theme.accent, 1.2)
                : (ctl.hovered ? Qt.darker(Theme.accent, 1.12) : Theme.accent))
        }
        contentItem: Label {
            text: ctl.text
            color: ctl.enabled ? Theme.accentText : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            leftPadding: 14; rightPadding: 14
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("New chat"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "close"; onClicked: root.close() }
        }

        Label {
            visible: root.errorText.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.danger
            text: root.errorText
        }

        Label {
            text: qsTr("Paste a bazarish:// invite link, or enter an alias beginning with %1").arg(App.aliasSigil)
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                TextArea {
                    id: targetText
                    wrapMode: TextArea.WrapAnywhere
                    color: Theme.text
                    placeholderTextColor: Theme.textDim
                    placeholderText: qsTr("bazarish://invite?...  or  %1alias").arg(App.aliasSigil)
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                }
            }
            IconButton {
                iconName: "qr"
                Layout.alignment: Qt.AlignTop
                tint: root.scanning ? Theme.green : Theme.text
                onClicked: root.scanning = !root.scanning
            }
        }

        Loader {
            active: root.scanning
            visible: root.scanning
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            sourceComponent: VideoOutput {
                id: preview
                MediaDevices { id: cameras }
                CaptureSession {
                    camera: Camera {
                        active: cameras.videoInputs.length > 0
                        onErrorOccurred: function(error, errorString) {
                            root.errorText = errorString
                            root.scanning = false
                        }
                    }
                    videoOutput: preview
                }
                QrScanner {
                    sink: preview.videoSink
                    onDecoded: function(text) {
                        root.scanning = false
                        targetText.text = text
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: cameras.videoInputs.length === 0
                    text: qsTr("No camera on this machine.")
                    color: Theme.textDim
                }
            }
        }

        Label {
            visible: root.problem.length > 0
            text: root.problem
            color: Theme.warn
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            visible: root.byAlias && root.problem.length === 0
            text: qsTr("The resolver hands back the descriptor of this alias.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // A contact request is admitted by nothing, and the protocol caps what
        // one may carry; the introduction is what is left over to write.
        FormField {
            id: intro
            label: qsTr("Introduction")
            text: root.kGreeting
            maximumLength: App.maxGreetingLength
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Send request")
                enabled: root.typed.length > 0 && root.problem.length === 0
                onClicked: root.startRequest(function() {
                    if (root.byAlias) {
                        root.session.addByAlias(root.typed, intro.text)
                    } else {
                        root.session.addByInvite(root.typed, intro.text)
                    }
                })
            }
        }
    }
}
