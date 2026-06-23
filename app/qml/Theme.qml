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

    // Neon - the brand's (darker) green, reserved here for emphasis:
    // titles/headers and positive status (a good state). Everything else stays
    // gray; the primary buttons are white (below), so this is the sole hue that
    // draws the eye. Uses the brandbook's dim-phosphor green, not the bright one.
    readonly property color neon:      "#1f7a14"
    // Accent - primary buttons / interactive emphasis are a near-white (the
    // formerly-neon buttons are now white); dark ink sits on the white fill.
    readonly property color accentInk: "#11151a"   // dark ink on a near-white fill
    readonly property color accent:    "#f2f4f2"
    readonly property color accentText: accentInk

    // Status. success is the neon (a good state stands out); warn and danger keep
    // a hue because an alert must stand out.
    readonly property color success:   neon
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

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontTitle: 17

    // Monospace everywhere (Roboto Mono, bundled; falls back to the system mono).
    readonly property string fontFamily: "Roboto Mono"
}
