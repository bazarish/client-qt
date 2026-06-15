import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Bazarish

Item {
    id: root
    property var session: null

    function facadeList() {
        var urls = []
        for (var i = 0; i < facadeModel.count; ++i) {
            var u = facadeModel.get(i).url.trim()
            if (u.length > 0) urls.push(u)
        }
        return urls
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 480)
        spacing: 14

        Label {
            text: "Connect to a server"
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Your profile needs a serving server to send and receive. "
                + "Everything stays end-to-end encrypted; a facade is just the last mile."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // One-link import: paste a bazarish://server/... link to fill everything.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: linkField
                Layout.fillWidth: true
                placeholderText: "Paste a bazarish://server/… link (fills everything)"
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
            }
            Button {
                text: "Apply"
                enabled: linkField.text.trim().length > 0
                onClicked: {
                    var info = root.session.parseServerLink(linkField.text)
                    if (info.serverFp && info.serverFp.length > 0) {
                        facadeModel.clear()
                        for (var i = 0; i < info.facades.length; ++i) facadeModel.append({ url: info.facades[i] })
                        if (facadeModel.count === 0) facadeModel.append({ url: "" })
                        fpField.text = info.serverFp
                    } else if (typeof window !== "undefined") {
                        window.showToast("Not a valid server link")
                    }
                }
            }
        }

        // Facade type (only HTTP for now; a dropdown of facade kinds later).
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Facade type"; color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
            ComboBox { model: ["HTTP facade"]; enabled: false; implicitWidth: 160 }
        }

        Label {
            text: "Facade URL(s) — tried in order, with failover:"
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }

        ListModel {
            id: facadeModel
            ListElement { url: "" }
        }
        Repeater {
            model: facadeModel
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    Layout.fillWidth: true
                    text: model.url
                    placeholderText: "http[s]://host:port/secret-path"
                    color: Theme.text
                    selectByMouse: true
                    onTextChanged: facadeModel.setProperty(index, "url", text)
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
                }
                IconButton {
                    text: "✕"
                    visible: facadeModel.count > 1
                    onClicked: facadeModel.remove(index)
                }
            }
        }
        Button {
            text: "＋ Add another facade"
            onClicked: facadeModel.append({ url: "" })
            background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
            contentItem: Label { text: parent.text; color: Theme.accent; horizontalAlignment: Text.AlignHCenter }
        }

        FormField { id: fpField; label: "Server fingerprint" }

        Button {
            Layout.fillWidth: true
            text: "Connect & subscribe"
            enabled: root.facadeList().length > 0 && fpField.text.trim().length > 0
            onClicked: root.session.connectServer(root.facadeList(), fpField.text.trim())
            background: Rectangle { radius: 10; color: parent.enabled ? Theme.accent : Theme.surfaceAlt }
            contentItem: Label { text: parent.text; color: Theme.accentText; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
