// Bazarish project (c) 2026
import QtQuick
import QtQuick.Layouts
import Bazarish

Label {
    id: root
    property string full: ""

    function fitted(available) {
        if (metrics.advanceWidth(full) <= available) {
            return full
        }
        let low = 0
        let high = full.length
        while (low < high) {
            const keep = Math.ceil((low + high) / 2)
            if (metrics.advanceWidth(root.joined(keep)) <= available) {
                low = keep
            } else {
                high = keep - 1
            }
        }
        return root.joined(low)
    }
    function joined(keep) {
        return full.slice(0, Math.ceil(keep / 2)) + "..." + full.slice(full.length - Math.floor(keep / 2))
    }

    FontMetrics {
        id: metrics
        font: root.font
    }

    // Measured from the whole line, not the fitted text: the text follows the width, the width must not follow it.
    Layout.preferredWidth: Math.ceil(metrics.advanceWidth(full)) + leftPadding + rightPadding
    // A hidden line is not laid out: its width follows its own text, so fitting it would loop.
    text: visible ? fitted(width - leftPadding - rightPadding) : full
    wrapMode: Text.NoWrap
}
