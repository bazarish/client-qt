// Bazarish project (c) 2026
pragma Singleton
import QtQuick
import Bazarish

QtObject {
    readonly property bool light: App.lightTheme

    readonly property color bg:        light ? "#eef1ee" : "#16191c"
    readonly property color deep:      light ? "#e2e7e3" : "#0d0f11"
    readonly property color surface:   light ? "#ffffff" : "#1b2026"
    readonly property color surfaceAlt: light ? "#e9eeea" : "#232a31"
    readonly property color border:    light ? "#cfd6d0" : "#262b2f"
    readonly property color border2:   light ? "#a3ada5" : "#5f6c77"
    readonly property color pressed:   light ? "#a3ada5" : "#333a40"

    readonly property color text:      light ? "#16191c" : "#d7dbd8"
    readonly property color textDim:   light ? "#4c554f" : "#9ea59e"
    readonly property color textFaint: light ? "#5d665f" : "#879195"

    readonly property color green:     light ? "#1f7a14" : "#2aa61b"
    readonly property color onGreen:   light ? "#f2f4f2" : accentInk
    readonly property color neon:      light ? green : "#39ff14"
    readonly property color accentInk: light ? "#f2f4f2" : "#11151a"
    readonly property color accent:    light ? "#1b2026" : "#f2f4f2"
    readonly property color accentText: accentInk
    readonly property color accentHover: light ? "#2b333d" : Qt.darker(accent, 1.12)
    readonly property color accentDown: light ? "#39434e" : Qt.darker(accent, 1.2)
    readonly property color hoverTint: light ? Qt.rgba(0, 0, 0, 0.04) : Qt.rgba(1, 1, 1, 0.04)

    readonly property color success:   green
    readonly property color warn:      light ? "#8f5200" : "#ffb454"
    readonly property color danger:    light ? "#c0261f" : "#ff5b54"
    readonly property color warnFill:  light ? "#ffbf5e" : warn
    readonly property color onWarn:    light ? "#16191c" : accentInk

    readonly property color bubbleOut: light ? "#dbeed4" : "#13261a"
    readonly property color bubbleIn:  light ? "#ffffff" : "#1b2026"
    readonly property color bubbleOutHeld: light ? "#cbe5c1" : "#1c3524"
    readonly property color bubbleInHeld: surfaceAlt
    readonly property color authorName: light ? green : "#7fd36f"
    readonly property color reactionMine: green
    readonly property color reactionMineEdge: light ? green : authorName
    readonly property color reactionMineInk: onGreen
    readonly property color knobOn: light ? text : onGreen

    readonly property int spacing: 12
    readonly property int radius: 10
    readonly property int radiusSmall: 6
    readonly property int rowInset: 6
    readonly property int avatar: 44
    readonly property int barHeight: 48
    readonly property real speechGain: 4
    // A press that takes focus closes a phone's keyboard and moves the window before it lifts.
    readonly property int pressFocus: App.mobile ? Qt.NoFocus : Qt.StrongFocus
    readonly property int narrowWidth: 560

    readonly property int iconInline: 15

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontLarge: 16
    readonly property int fontTitle: 17

    readonly property string fontFamily: "Roboto Mono"
    readonly property string emojiFontFamily: "Twemoji Mozilla"
    // The emoji font only for what is emoji: it has blank glyphs for plain digits.
    function reactionFont(reaction) {
        return /^[\x20-\x7E]+$/.test(reaction) ? fontFamily : emojiFontFamily
    }
}
