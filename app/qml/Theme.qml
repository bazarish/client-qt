pragma Singleton
import QtQuick
import Bazarish

// The Bazarish brand: "terminal / neon" - a dark gray screen with a single
// neon-green accent (the cursor). Hex values mirror the brand token sheet
// (tokens.css). The one-accent rule: green is reserved for the cursor / one
// primary action per view; everything else is gray on dark, and a glyph on a
// green fill goes dark (onAccent). The light theme is paper with ink: the primary
// action turns near-black, and the neon drops to the dim green, as the brand
// prescribes on a light surface.
QtObject {
    id: theme

    readonly property bool light: App.lightTheme

    // Foundation - the screen.
    readonly property color bg:        light ? "#eef1ee" : "#16191c"  // app canvas
    readonly property color deep:      light ? "#e2e7e3" : "#0d0f11"  // input fields / code areas
    readonly property color surface:   light ? "#ffffff" : "#1b2026"  // cards, list rows
    readonly property color surfaceAlt: light ? "#e9eeea" : "#232a31"  // elevated rows / hover
    readonly property color border:    light ? "#cfd6d0" : "#262b2f"  // hairline borders
    readonly property color border2:   light ? "#a3ada5" : "#5f6c77"  // stronger dividers
    readonly property color pressed:   light ? "#a3ada5" : "#333a40"  // a pressed row or button

    // Ink - text.
    readonly property color text:      light ? "#16191c" : "#d7dbd8"  // primary
    readonly property color textDim:   light ? "#4c554f" : "#9ea59e"  // secondary
    readonly property color textFaint: light ? "#5d665f" : "#879195"  // tertiary / comments

    // Green - the brand's dim-phosphor green (brandbook --bz-green-dim), the
    // workhorse accent reserved for emphasis: titles/headers and positive status
    // (a good state). Everything else stays gray; the primary buttons are the
    // accent (below), so this is the main hue that draws the eye. Not the bright one.
    readonly property color green:     light ? "#1f7a14" : "#2aa61b"  // the brand's own fails AA as text on the dark grounds
    readonly property color onGreen:   light ? "#f2f4f2" : accentInk
    // Neon - the brightest brand phosphor green (brandbook --bz-green, the cursor
    // accent). Reserved for the rare, must-be-unmistakable highlights: the active
    // conversation/account name, and exceptional or destructive dialogs (e.g.
    // delete account). Used sparingly - never as a fill on text.
    readonly property color neon:      light ? green : "#39ff14"
    // Accent - primary buttons / interactive emphasis: near-white on the dark
    // screen, near-black on the light one; accentInk is the ink on that fill.
    readonly property color accentInk: light ? "#f2f4f2" : "#11151a"
    readonly property color accent:    light ? "#1b2026" : "#f2f4f2"
    readonly property color accentText: accentInk
    readonly property color accentHover: light ? "#2b333d" : Qt.darker(accent, 1.12)
    readonly property color accentDown: light ? "#39434e" : Qt.darker(accent, 1.2)
    // A row a pointer is over, before it is chosen.
    readonly property color hoverTint: light ? Qt.rgba(0, 0, 0, 0.04) : Qt.rgba(1, 1, 1, 0.04)

    // Status. success is the green (a good state stands out); warn and danger keep
    // a hue because an alert must stand out.
    readonly property color success:   green
    readonly property color warn:      light ? "#8f5200" : "#ffb454"
    readonly property color danger:    light ? "#c0261f" : "#ff5b54"
    // A warning laid as a band keeps its yellow in both themes, with dark ink on it.
    readonly property color warnFill:  light ? "#ffbf5e" : warn
    readonly property color onWarn:    light ? "#16191c" : accentInk

    // Message bubbles: outgoing carries a faint green-tinted surface (still not
    // the neon accent), incoming is the plain surface.
    readonly property color bubbleOut: light ? "#dbeed4" : "#13261a"
    readonly property color bubbleIn:  light ? "#ffffff" : "#1b2026"
    // A message under a held finger or its open menu: a row's hover tint, in its bubble's hue.
    readonly property color bubbleOutHeld: light ? "#cbe5c1" : "#1c3524"
    readonly property color bubbleInHeld: surfaceAlt
    readonly property color authorName: light ? green : "#7fd36f"
    // Our own reaction is filled green; on the dark screen a lighter edge keeps it over 3:1.
    readonly property color reactionMine: green
    readonly property color reactionMineEdge: light ? green : authorName
    readonly property color reactionMineInk: onGreen
    readonly property color knobOn: light ? text : onGreen

    readonly property int spacing: 12
    readonly property int radius: 10
    readonly property int radiusSmall: 6
    // A selected row's outline keeps this far from the list's edge and from its neighbours.
    readonly property int rowInset: 6
    readonly property int avatar: 44
    // The action bar under the chat list and the composer beside it are one
    // horizontal line across the window, so they share a resting height.
    readonly property int barHeight: 48
    // Speech sits low in the range a level can show, so what is drawn is lifted to
    // where the eye reads it.
    readonly property real speechGain: 4
    // A press that takes the focus closes a phone's keyboard, and the window moves
    // under the finger before it lifts: the press is lost. Keys still reach buttons on a desktop.
    readonly property int pressFocus: App.mobile ? Qt.NoFocus : Qt.StrongFocus
    // Below this the two panes cannot both be useful, so the actions collapse
    // into one menu and the window shows a single pane.
    readonly property int narrowWidth: 560

    // A drawing that stands beside a label - in a button, in a row - is sized to
    // the text it names, not to an icon button's square.
    readonly property int iconInline: 15

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontLarge: 16
    readonly property int fontTitle: 17

    // Monospace everywhere (Roboto Mono, bundled; falls back to the system mono).
    readonly property string fontFamily: "Roboto Mono"
    // Colour-emoji family (Twemoji Mozilla, bundled), also the application font's fallback.
    readonly property string emojiFontFamily: "Twemoji Mozilla"
    // The emoji font only for what is emoji: it has blank glyphs for plain digits.
    function reactionFont(reaction) {
        return /^[\x20-\x7E]+$/.test(reaction) ? fontFamily : emojiFontFamily
    }
}
