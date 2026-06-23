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

    // Accent - the cursor (the only accent).
    readonly property color green:     "#39ff14"
    readonly property color greenDim:  "#1f7a14"
    // Ink on a green fill. Not named "onGreen": QML reads an "on"-prefixed
    // identifier as a signal handler.
    readonly property color accentInk: "#11151a"
    readonly property color accent:    green
    readonly property color accentText: accentInk

    // Status (green doubles as success).
    readonly property color success:   "#39ff14"
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
