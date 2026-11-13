// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Popup {
    id: root
    property var session: null
    readonly property string uri: session ? session.ownInvite : ""
    property string publishing: ""
    readonly property string unavailable: root.publishing.length > 0 ? root.publishing
        : (session && root.uri.length === 0 ? session.ownInviteProblem : "")
    readonly property bool hasInvite: uri.length > 0
    readonly property var aliasRows: session ? session.aliasHoldings : []
    readonly property string aliasNote: session ? session.aliasNote : ""
    readonly property bool hasAliases: aliasRows.length > 0
    property bool showQr: false
    readonly property int kLinkLines: 4
    readonly property int kTagPadding: 10
    readonly property int kHeaderMargin: 14
    readonly property int kContentInset: 18
    readonly property string kI2pHost: ".i2p"
    readonly property int tagWidth: Math.ceil(Math.max(botTag.advanceWidth, wwwTag.advanceWidth,
        i2pTag.advanceWidth)) + 2 * kTagPadding
    signal back()

    component RegistryItem: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 44
        radius: 8
        color: Theme.surface
        border.color: Theme.border
    }

    component RegistryTag: Rectangle {
        property alias text: tagLabel.text
        Layout.preferredWidth: root.tagWidth
        Layout.fillHeight: true
        radius: 8
        color: Theme.surfaceAlt
        border.color: Theme.border
        Label {
            id: tagLabel
            anchors.centerIn: parent
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
    }

    component TagMetrics: TextMetrics {
        font.pixelSize: Theme.fontSmall
        font.weight: Font.DemiBold
    }
    TagMetrics { id: botTag; text: qsTr("Bot") }
    TagMetrics { id: wwwTag; text: qsTr("WWW") }
    TagMetrics { id: i2pTag; text: qsTr("I2P") }

    component RegistrySite: RegistryItem {
        id: site
        property string address: ""
        property bool copied: false
        readonly property int kCopiedMs: 2000
        RowLayout {
            id: siteRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 4
            spacing: 2
            Label {
                text: site.address.replace(/^https?:\/\//, "")
                color: Theme.text
                elide: Text.ElideMiddle
                Layout.rightMargin: 6
                Layout.maximumWidth: siteRow.width - copySite.width - openSite.width
                    - siteRow.spacing * 2 - Layout.rightMargin
            }
            IconButton {
                id: copySite
                implicitWidth: 36
                implicitHeight: 36
                iconName: site.copied ? "check" : "copy"
                tint: site.copied ? Theme.success : Theme.textDim
                Accessible.name: qsTr("Copy")
                onClicked: {
                    App.copyText(site.address)
                    site.copied = true
                    copiedAgo.restart()
                }
                Timer { id: copiedAgo; interval: site.kCopiedMs; onTriggered: site.copied = false }
            }
            IconButton {
                id: openSite
                implicitWidth: 36
                implicitHeight: 36
                iconName: "external"
                tint: Theme.textDim
                Accessible.name: qsTr("Open")
                onClicked: Qt.openUrlExternally(site.address)
            }
            Item { Layout.fillWidth: true }
        }
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 24 : 460)
    margins: Theme.spacing
    padding: 0
    onAboutToShow: sheetScroll.toTop()
    onOpened: {
        publishing = ""
        showQr = false
    }

    background: DialogFrame { }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: root.kHeaderMargin
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
        Hairline { }

        ScrollColumn {
            id: sheetScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            inset: root.kContentInset
            spacing: 12

            Label {
                text: root.hasAliases
                    ? qsTr("You can be added as a contact by alias or by the full descriptor")
                    : qsTr("Anyone with the link can reach you")
                color: Theme.textDim
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ColumnLayout {
                visible: root.unavailable.length > 0
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: qsTr("No invite yet: %1").arg(root.unavailable)
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
                    onClicked: if (root.session) { root.session.publishPersonalDest(); root.publishing = qsTr("Publishing. This takes a few minutes") }
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
                                && !root.session.aliasBusy && root.session.primaryDevice
                            onClicked: wordDialog.ask(aliasRow.modelData.alias, aliasRow.modelData.word)
                        }
                        Toggle {
                            checked: aliasRow.modelData.resolving
                            enabled: root.session !== null && root.session.connected
                                && !root.session.aliasBusy && root.session.primaryDevice
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
                    background: Rectangle { radius: 8; color: Theme.control; border.color: Theme.controlEdge }
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
                        color: copyBtn.copied ? Theme.greenFill : (copyBtn.enabled ? Theme.accent : Theme.surfaceAlt)
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                    contentItem: IconLabel {
                        name: "copy"
                        color: copyBtn.copied ? Theme.greenInk : (copyBtn.enabled ? Theme.accentText : Theme.textDim)
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
                        && root.session.primaryDevice
                    onClicked: root.session.activateAliasServicing()
                }
            }

            Hairline {
                visible: root.session !== null
            }
            Label {
                visible: root.session !== null
                text: qsTr("A short name (alias) can be bought in the registry:")
                color: Theme.textDim
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                visible: root.session !== null
                Layout.fillWidth: true
                RegistryTag { text: qsTr("Bot") }
                RegistryItem {
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        text: root.session ? root.session.registryBot : ""
                        color: Theme.accent
                        font.underline: botHover.hovered
                        HoverHandler { id: botHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                root.session.addByAlias(root.session.registryBot, "")
                                root.close()
                            }
                        }
                    }
                }
            }
            Repeater {
                model: root.session ? root.session.registrySites : []
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    RegistryTag { text: modelData.includes(root.kI2pHost) ? i2pTag.text : wwwTag.text }
                    RegistrySite { address: modelData }
                }
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
