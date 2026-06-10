pragma Singleton
import QtQuick

// Calm, minimal, privacy-first palette (Signal-like). Toggle `dark` to switch.
QtObject {
    id: theme
    property bool dark: false

    readonly property color bg:        dark ? "#15171a" : "#ffffff"
    readonly property color surface:   dark ? "#1d2024" : "#f5f6f8"
    readonly property color surfaceAlt: dark ? "#262a2f" : "#eceef1"
    readonly property color border:    dark ? "#2d3137" : "#e3e5e9"
    readonly property color text:      dark ? "#e7e9ec" : "#1b1b1f"
    readonly property color textDim:   dark ? "#9aa0a8" : "#6b7280"
    readonly property color accent:    "#2b6cb0"
    readonly property color accentText: "#ffffff"
    readonly property color bubbleOut: dark ? "#2b4a6b" : "#dbeafe"
    readonly property color bubbleIn:  dark ? "#23272c" : "#f0f1f4"
    readonly property color danger:    "#c0392b"
    readonly property color success:   "#2f855a"

    readonly property int spacing: 12
    readonly property int radius: 14
    readonly property int avatar: 44

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontTitle: 17
}
