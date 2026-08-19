import QtQuick
import QtQuick.Shapes
import Bazarish

// A drawn icon, not a character. The interface used font glyphs - some of them
// emoji - which a system substitutes from whatever font it has: different metrics,
// different weight, colour emoji next to a monochrome line drawing. These are
// paths on a 24x24 grid, so every system gets the same shape at the same weight,
// and `color` follows the theme like any other element.
Item {
    id: root
    // One of: close, back, chevron, gear, info, search, call, attach, pin, copy,
    // refresh, edit, more, plus, check, up, down, forward, dot, stop, send.
    property string name: ""
    property color color: Theme.text
    property real size: 16
    // Line weight in grid units; the grid is 24 wide whatever `size` is.
    property real weight: 2

    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        scale: root.size / 24
        transformOrigin: Item.TopLeft
        width: 24
        height: 24

        // Stroked shapes: everything that reads as a line drawing.
        ShapePath {
            strokeColor: root.name === "dot" ? "transparent" : root.color
            fillColor: "transparent"
            strokeWidth: root.weight
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: {
                    switch (root.name) {
                    case "close":   return "M 6 6 L 18 18 M 18 6 L 6 18"
                    case "back":    return "M 15 5 L 8 12 L 15 19"
                    case "forward": return "M 9 5 L 16 12 L 9 19"
                    case "chevron": return "M 6 9 L 12 15 L 18 9"
                    case "up":      return "M 12 19 L 12 6 M 6 12 L 12 6 L 18 12"
                    case "down":    return "M 12 5 L 12 18 M 6 12 L 12 18 L 18 12"
                    case "check":   return "M 5 13 L 10 18 L 19 7"
                    case "plus":    return "M 12 5 L 12 19 M 5 12 L 19 12"
                    case "search":  return "M 11 4 A 7 7 0 1 1 10.99 4 M 16 16 L 20 20"
                    case "refresh": return "M 20 12 A 8 8 0 1 1 17 5.7 M 17 2.5 L 17 6.5 L 13 6.5"
                    case "gear":    return "M 12 8.5 A 3.5 3.5 0 1 1 11.99 8.5 "
                                         + "M 12 2.5 L 12 5 M 12 19 L 12 21.5 "
                                         + "M 21.5 12 L 19 12 M 5 12 L 2.5 12 "
                                         + "M 18.7 5.3 L 16.9 7.1 M 7.1 16.9 L 5.3 18.7 "
                                         + "M 18.7 18.7 L 16.9 16.9 M 7.1 7.1 L 5.3 5.3"
                    case "info":    return "M 12 3 A 9 9 0 1 1 11.99 3 M 12 11 L 12 17 M 12 7.5 L 12 8"
                    case "call":    return "M 6 3 L 9.5 3 L 11 8 L 8.5 9.5 "
                                         + "A 11 11 0 0 0 14.5 15.5 L 16 13 L 21 14.5 L 21 18 "
                                         + "A 3 3 0 0 1 18 21 A 18 18 0 0 1 3 6 A 3 3 0 0 1 6 3"
                    case "attach":  return "M 17 8 L 9.5 15.5 A 3 3 0 0 0 13.5 19.5 L 20 13 "
                                         + "A 5.5 5.5 0 0 0 12.5 5.5 L 6 12 "
                                         + "A 8 8 0 0 0 17.5 23"
                    case "pin":     return "M 9 3 L 15 3 L 14 10 L 18 13 L 6 13 L 10 10 Z M 12 13 L 12 21"
                    case "copy":    return "M 9 9 L 20 9 L 20 20 L 9 20 Z M 5 15 L 4 15 L 4 4 L 15 4 L 15 5"
                    case "edit":    return "M 4 20 L 4 16 L 16 4 L 20 8 L 8 20 Z M 14 6 L 18 10"
                    case "stop":    return "M 7 7 L 17 7 L 17 17 L 7 17 Z"
                    case "send":    return "M 3 12 L 21 4 L 14 21 L 11.5 13.5 Z M 11.5 13.5 L 21 4"
                    default:        return ""
                    }
                }
            }
        }

        // Filled shapes: the few that are solid rather than drawn.
        ShapePath {
            strokeColor: "transparent"
            fillColor: root.name === "more" || root.name === "dot" ? root.color : "transparent"
            PathSvg {
                path: {
                    switch (root.name) {
                    case "more": return "M 12 4 A 1.6 1.6 0 1 1 11.99 4 Z "
                                      + "M 12 10.4 A 1.6 1.6 0 1 1 11.99 10.4 Z "
                                      + "M 12 16.8 A 1.6 1.6 0 1 1 11.99 16.8 Z"
                    case "dot":  return "M 12 8 A 4 4 0 1 1 11.99 8 Z"
                    default:     return ""
                    }
                }
            }
        }
    }
}
