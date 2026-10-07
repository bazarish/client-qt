pragma Singleton
import QtQuick

// The Bazarish brand: "terminal / neon" - a dark gray screen with a single
// neon-green accent (the cursor). One brand, dark only. Hex values mirror the
// brand token sheet (tokens.css). The one-accent rule: green is reserved for the
// cursor / one primary action per view; everything else is gray on dark, and a
// glyph on a green fill goes dark (onAccent).
QtObject {
    id: theme

    // Foundation - the screen.
    readonly property color bg:        "#16191c"  // app canvas
    readonly property color deep:      "#0d0f11"  // input fields / code areas
    readonly property color surface:   "#1b2026"  // cards, list rows
    readonly property color surfaceAlt: "#232a31"  // elevated rows / hover
    readonly property color border:    "#262b2f"  // hairline borders
    readonly property color border2:   "#333a40"  // stronger dividers

    // Ink - text.
    readonly property color text:      "#d7dbd8"  // primary
    readonly property color textDim:   "#8b948c"  // secondary
    readonly property color textFaint: "#5e676a"  // tertiary / comments

    // Green - the brand's dim-phosphor green (brandbook --bz-green-dim), the
    // workhorse accent reserved for emphasis: titles/headers and positive status
    // (a good state). Everything else stays gray; the primary buttons are white
    // (below), so this is the main hue that draws the eye. Not the bright one.
    readonly property color green:     "#1f7a14"
    // Neon - the brightest brand phosphor green (brandbook --bz-green, the cursor
    // accent). Reserved for the rare, must-be-unmistakable highlights: the active
    // conversation/account name, and exceptional or destructive dialogs (e.g.
    // delete account). Used sparingly - never as a fill on text.
    readonly property color neon:      "#39ff14"
    // Accent - primary buttons / interactive emphasis are a near-white (the
    // formerly-neon buttons are now white); dark ink sits on the white fill.
    readonly property color accentInk: "#11151a"   // dark ink on a near-white fill
    readonly property color accent:    "#f2f4f2"
    readonly property color accentText: accentInk

    // Status. success is the green (a good state stands out); warn and danger keep
    // a hue because an alert must stand out.
    readonly property color success:   green
    readonly property color warn:      "#ffb454"
    readonly property color danger:    "#ff5b54"

    // Message bubbles: outgoing carries a faint green-tinted surface (still not
    // the neon accent), incoming is the plain surface.
    readonly property color bubbleOut: "#13261a"
    readonly property color bubbleIn:  "#1b2026"

    readonly property int spacing: 12
    readonly property int radius: 10
    readonly property int radiusSmall: 6
    readonly property int avatar: 44
    // The action bar under the chat list and the composer beside it are one
    // horizontal line across the window, so they share a resting height.
    readonly property int barHeight: 48
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
    // Colour-emoji family (Twemoji Mozilla, bundled). Use it on a Label that shows an
    // emoji, together with `renderType: Text.NativeRendering` - the default Qt Quick
    // distance-field renderer draws emoji monochrome (invisible on the dark theme).
    readonly property string emojiFontFamily: "Twemoji Mozilla"
}
