// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    property string uri: ""
    property string unavailable: ""
    readonly property bool hasInvite: uri.length > 0
    readonly property var aliasRows: session ? session.aliasHoldings : []
    readonly property string aliasNote: session ? session.aliasNote : ""
    readonly property bool hasAliases: aliasRows.length > 0
    property bool showQr: false
    readonly property int kLinkLines: 4
    signal back()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    margins: Theme.spacing
    padding: 18
    onOpened: {
        uri = session ? session.ownInvite : ""
        unavailable = ""
        showQr = false
        if (session && uri.length === 0) {
            session.requestInvite()
        }
    }

    background: DialogFrame { }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onInviteReady(u) { root.uri = u; root.unavailable = "" }
        function onInviteUnavailable(reason) { root.uri = ""; root.unavailable = reason }
        function onI2pStatusChanged() {
            if (root.unavailable.length > 0 && root.session) {
                root.session.requestInvite()
            }
        }
    }

    contentItem: ScrollColumn {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            IconButton { iconName: "back"; onClicked: root.back() }
            Label { text: qsTr("My invite"); color: Theme.green; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton {
                iconName: "qr"
                visible: root.hasAliases && root.hasInvite
                tint: root.showQr ? Theme.accent : Theme.textDim
                onClicked: root.showQr = !root.showQr
            }
            IconButton { iconName: "close"; onClicked: root.close() }
        }
        Label {
            text: root.hasAliases
                ? qsTr("You can be added as a contact by alias or by the full descriptor.")
                : qsTr("Anyone with the link can reach you.")
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ColumnLayout {
            visible: root.unavailable.length > 0
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("No invite yet: %1.").arg(root.unavailable)
                color: Theme.warn
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("An invite cannot be formed before your destination is up. Publishing takes a few minutes.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                focusPolicy: Theme.pressFocus
                text: qsTr("Publish my destination")
                Layout.fillWidth: true
                onClicked: if (root.session) { root.session.publishPersonalDest(); root.unavailable = qsTr("Publishing. This takes a few minutes") }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.green }
                contentItem: IconLabel { name: "broadcast"; color: Theme.green }
            }
        }

        ColumnLayout {
            visible: root.hasAliases && !root.showQr
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.aliasRows
                delegate: RowLayout {
                    id: aliasRow
                    required property var modelData
                    property bool copied: false
                    readonly property string shared: "!" + modelData.alias
                        + (modelData.word.length > 0 ? "-" + modelData.word : "")
                    Layout.fillWidth: true
                    spacing: 8
                    Timer { id: aliasCopied; interval: 1500; onTriggered: aliasRow.copied = false }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (!root.session) {
                                    return
                                }
                                App.copyText(aliasRow.shared)
                                aliasRow.copied = true
                                aliasCopied.restart()
                            }
                        }
                        Label {
                            text: "!"
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontLarge
                        }
                        Label {
                            text: aliasRow.modelData.alias
                            color: aliasRow.copied
                                ? Theme.green
                                : (aliasRow.modelData.resolving ? Theme.text : Theme.textDim)
                            font.pixelSize: Theme.fontLarge
                        }
                        Label {
                            visible: aliasRow.modelData.word.length > 0
                            text: "-" + aliasRow.modelData.word
                            color: aliasRow.copied ? Theme.green : Theme.textFaint
                            font.pixelSize: Theme.fontLarge
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Item {
                            visible: aliasRow.modelData.word.length === 0
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        text: aliasRow.copied
                            ? qsTr("Copied to clipboard")
                            : (aliasRow.modelData.resolving
                                ? (aliasRow.modelData.live
                                    ? aliasRow.modelData.term
                                    : qsTr("pointing it here…"))
                                : qsTr("answers nobody"))
                        color: aliasRow.copied ? Theme.green : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                    IconButton {
                        iconName: "key"
                        enabled: root.session !== null && root.session.connected
                            && !root.session.aliasBusy
                        onClicked: wordDialog.ask(aliasRow.modelData.alias, aliasRow.modelData.word)
                    }
                    Toggle {
                        checked: aliasRow.modelData.resolving
                        enabled: root.session !== null && root.session.connected
                            && !root.session.aliasBusy
                        onToggled: root.session.setAliasBinding(
                            aliasRow.modelData.alias, checked)
                    }
                }
            }
        }
        Label {
            visible: root.hasAliases && !root.showQr && root.aliasNote.length > 0
            Layout.fillWidth: true
            text: root.aliasNote
            color: Theme.warn
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        QrView {
            Layout.alignment: Qt.AlignHCenter
            visible: root.hasInvite && (!root.hasAliases || root.showQr)
            text: root.uri
        }
        FontMetrics { id: linkMetrics; font: linkArea.font }
        ScrollView {
            visible: !root.hasAliases
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasInvite
                ? linkArea.topPadding + linkArea.bottomPadding
                    + root.kLinkLines * Math.ceil(linkMetrics.lineSpacing)
                : 56
            TextArea {
                id: linkArea
                readOnly: true
                wrapMode: TextArea.WrapAnywhere
                text: root.uri
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: linkArea.leftPadding
                    anchors.rightMargin: linkArea.rightPadding
                    anchors.topMargin: linkArea.topPadding
                    visible: linkArea.length === 0 && root.unavailable.length === 0
                    text: qsTr("Waiting for the server…")
                    color: Theme.textDim
                    wrapMode: Text.Wrap
                }
            }
        }
        ButtonRow {
            id: inviteActions
            Layout.fillWidth: true
            Button {
                id: copyBtn
                focusPolicy: Theme.pressFocus
                hoverEnabled: true
                property bool copied: false
                Layout.fillWidth: true
                text: copied ? qsTr("Copied") : qsTr("Copy link")
                enabled: root.hasInvite
                onClicked: {
                    App.copyText(root.uri)
                    copied = true; copiedTimer.restart()
                }
                background: Rectangle {
                    radius: 10
                    color: copyBtn.copied ? Theme.success : (copyBtn.enabled ? Theme.accent : Theme.surfaceAlt)
                    Behavior on color { ColorAnimation { duration: 200 } }
                }
                contentItem: IconLabel {
                    name: "copy"
                    color: copyBtn.enabled ? Theme.accentText : Theme.textDim
                }
                Timer { id: copiedTimer; interval: 1500; onTriggered: copyBtn.copied = false }
            }
            MenuButton {
                Layout.fillWidth: inviteActions.columns === 1
                iconName: "bang"
                text: root.session && root.session.aliasBusy
                    ? qsTr("Asking…")
                    : qsTr("Check my aliases")
                enabled: root.session && root.session.connected && !root.session.aliasBusy
                onClicked: root.session.activateAliasServicing()
            }
        }
    }

    // The word gates every lookup of the name: without it the registry answers as for a free name.
    Dialog {
        id: wordDialog
        property string alias: ""
        function ask(name, word) {
            alias = name
            wordField.text = word
            open()
        }
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(380, root.width)
        background: DialogFrame { }
        header: Label {
            text: qsTr("Word for %1").arg("!" + wordDialog.alias)
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            padding: 14
        }
        footer: DialogButtons {
            acceptText: qsTr("Save")
            onAccepted: wordDialog.accept()
            onRejected: wordDialog.reject()
        }
        onAccepted: if (root.session) {
            root.session.setAliasWord(wordDialog.alias, wordField.text)
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.bottomMargin: 0
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                text: qsTr("The alias is then found only with the word after a dash. Empty takes it off.")
            }
            TextField {
                id: wordField
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.topMargin: 0
                maximumLength: App.aliasWordMaxLength
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9]*/ }
                placeholderText: qsTr("Word")
            }
        }
    }
}
