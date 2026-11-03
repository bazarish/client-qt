// Bazarish project (c) 2026
import QtQuick
import Bazarish

Label {
    id: root
    property string value: ""
    property bool whole: false
    property string replacement: ""
    readonly property var groups: value.match(/.{1,4}/g) || []

    function fitted(available) {
        const all = groups.join(" ")
        if (groups.length < 3 || metrics.advanceWidth(all) <= available) {
            return all
        }
        for (let keep = groups.length - 1; keep > 2; --keep) {
            const head = Math.ceil(keep / 2)
            const tail = Math.floor(keep / 2)
            const candidate = groups.slice(0, head).join(" ") + " … "
                + groups.slice(groups.length - tail).join(" ")
            if (metrics.advanceWidth(candidate) <= available) {
                return candidate
            }
        }
        return groups[0] + " … " + groups[groups.length - 1]
    }

    FontMetrics {
        id: metrics
        font: root.font
    }

    text: replacement.length > 0 ? replacement : (whole ? groups.join(" ") : fitted(width))
    wrapMode: whole ? Text.WordWrap : Text.NoWrap
}
