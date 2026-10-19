import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    margins: Theme.spacing
    padding: 18
    property string errorText: ""
    property string prefillTarget: ""
    readonly property string kGreeting: qsTr("Hi, add me?")
    readonly property int kInputLine: 38
    readonly property int kInputLimit: 120
    readonly property int kInputPadding: 18
    readonly property int inputHeight: Math.min(kInputLimit,
        Math.max(kInputLine, Math.ceil(Math.max(targetText.contentHeight,
            targetHint.visible ? targetHint.contentHeight : 0)) + kInputPadding))

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
        targetText.text = root.prefillTarget
        errorText = ""
        targetText.forceActiveFocus()
    }
    onClosed: {
        errorText = ""
        targetText.text = ""
        intro.text = root.kGreeting
    }

    function openAlias(alias) {
        root.prefillTarget = App.aliasSigil + alias
        root.open()
        root.prefillTarget = ""
    }

    function openInvite(uri) {
        root.prefillTarget = uri
        root.open()
        root.prefillTarget = ""
    }

    background: DialogFrame { }

    // The add-contact actions run on the worker thread (a lookup plus a sealed
    // delivery with retries - slow over I2P). The conversation the request belongs
    // to opens right away and the progress is written into it, so this sheet has
    // nothing left to wait for: it fires the request and closes.
    function startRequest(fn) { errorText = ""; fn(); close() }

    function submit() {
        if (root.typed.length === 0 || root.problem.length > 0) {
            return
        }
        root.startRequest(function() {
            if (root.byAlias) {
                root.session.addByAlias(root.typed, intro.text)
            } else {
                root.session.addByInvite(root.typed, intro.text)
            }
        })
    }

    // Primary action button: near-white accent fill, darker on hover/press, so it
    // reads clearly against the dark popup (the default Basic Button blends in).
    component ActionButton: Button {
        id: ctl
        hoverEnabled: true
        background: Rectangle {
            radius: 10
            color: !ctl.enabled ? Theme.surfaceAlt
                : (ctl.down ? Theme.accentDown
                : (ctl.hovered ? Theme.accentHover : Theme.accent))
        }
        contentItem: Label {
            text: ctl.text
            color: ctl.enabled ? Theme.accentText : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            leftPadding: 14; rightPadding: 14
        }
    }

    contentItem: ScrollColumn {
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
            text: qsTr("Paste an invite link or a short alias")
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
                Layout.preferredHeight: root.inputHeight
                TextArea {
                    id: targetText
                    wrapMode: TextArea.WrapAnywhere
                    color: Theme.text
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                    Label {
                        id: targetHint
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: targetText.leftPadding
                        anchors.rightMargin: targetText.rightPadding
                        anchors.topMargin: targetText.topPadding
                        visible: targetText.length === 0 && targetText.preeditText.length === 0
                        // U+2060 keeps the sigil on the line of the word it marks.
                        text: qsTr("bazarish://invite?...  or  %1alias").arg(App.aliasSigil + "\u2060")
                        color: Theme.textDim
                        font: targetText.font
                        wrapMode: Text.Wrap
                    }
                    Keys.onReturnPressed: function(event) { root.submit(); event.accepted = true }
                    Keys.onEnterPressed: function(event) { root.submit(); event.accepted = true }
                }
            }
            QrScanButton {
                Layout.alignment: Qt.AlignTop
                onDecoded: function(text) { targetText.text = text }
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
                onClicked: root.submit()
            }
        }
    }
}
