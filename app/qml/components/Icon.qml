// Bazarish project (c) 2026
import QtQuick
import QtQuick.Shapes
import Bazarish

Item {
    id: root
    property string name: ""
    property color color: Theme.text
    property real size: 16
    // Line weight in grid units; the grid is 24 wide whatever `size` is.
    property real weight: 2

    implicitWidth: size
    implicitHeight: size

    Shape {
        id: shape
        anchors.centerIn: parent
        // Set at runtime: preferredRendererType exists only from Qt 6.6 on.
        Component.onCompleted: if (shape.hasOwnProperty("preferredRendererType")) {
            shape.preferredRendererType = Shape.CurveRenderer
        }
        scale: root.size / 24
        transformOrigin: Item.Center
        width: 24
        height: 24

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
                    case "clock":   return "M 21 12 A 9 9 0 1 0 3 12 A 9 9 0 1 0 21 12 Z M 12 7 V 12 L 15.5 14"
                    case "sun":     return "M 16 12 A 4 4 0 1 0 8 12 A 4 4 0 1 0 16 12 Z "
                                         + "M 12 2 L 12 4 M 12 20 L 12 22 M 2 12 L 4 12 M 20 12 L 22 12 "
                                         + "M 4.9 4.9 L 6.3 6.3 M 17.7 17.7 L 19.1 19.1 "
                                         + "M 4.9 19.1 L 6.3 17.7 M 17.7 6.3 L 19.1 4.9"
                    case "moon":    return "M 21 12.8 A 9 9 0 1 1 11.2 3 A 7 7 0 0 0 21 12.8 Z"
                    case "rotate-right": return "M 19 13 A 7 7 0 1 1 12 6 M 9 3 L 12 6 L 9 9"
                    case "rotate-left":  return "M 5 13 A 7 7 0 1 0 12 6 M 15 3 L 12 6 L 15 9"
                    case "back":    return "M 15 5 L 8 12 L 15 19"
                    case "forward": return "M 9 5 L 16 12 L 9 19"
                    case "forwarded": return "M 5 6 L 11 12 L 5 18 M 12 6 L 18 12 L 12 18"
                    case "link":    return "M 10 14 A 4 4 0 0 0 15.7 14 L 18.2 11.5 "
                        + "A 4 4 0 0 0 12.5 5.8 L 11.2 7.1 "
                        + "M 14 10 A 4 4 0 0 0 8.3 10 L 5.8 12.5 "
                        + "A 4 4 0 0 0 11.5 18.2 L 12.8 16.9"
                    case "bang":    return "M 12 4 L 12 14 M 12 18 L 12 18.5"
                    case "chevron": return "M 6 9 L 12 15 L 18 9"
                    case "up":      return "M 12 19 L 12 6 M 6 12 L 12 6 L 18 12"
                    case "down":    return "M 12 5 L 12 18 M 6 12 L 12 18 L 18 12"
                    case "check":   return "M 5 13 L 10 18 L 19 7"
                    case "plus":    return "M 12 5 L 12 19 M 5 12 L 19 12"
                    case "search":  return "M 11 4 A 7 7 0 1 1 10.99 4 M 16 16 L 20 20"
                    case "refresh": return "M 20 12 A 8 8 0 1 1 17 5.7 M 17 2.5 L 17 6.5 L 13 6.5"
                    case "info":    return "M 12 3 A 9 9 0 1 1 11.99 3 M 12 11 L 12 17 M 12 7.5 L 12 8"
                    case "call":    return "M 6 3 L 9.5 3 L 11 8 L 8.5 9.5 "
                                         + "A 11 11 0 0 0 14.5 15.5 L 16 13 L 21 14.5 L 21 18 "
                                         + "A 3 3 0 0 1 18 21 A 18 18 0 0 1 3 6 A 3 3 0 0 1 6 3"
                    case "image":   return "M 4 5 H 20 V 19 H 4 Z M 4 16 L 9 11 L 13 15 "
                        + "M 13 15 L 16 12 L 20 16 M 15.5 8.5 A 1.2 1.2 0 1 1 15.49 8.5"
                    case "speaker": return "M 4 9.5 H 7.5 L 12 5.5 V 18.5 L 7.5 14.5 H 4 Z "
                        + "M 15.5 9 A 4 4 0 0 1 15.5 15 M 18 6.5 A 7.5 7.5 0 0 1 18 17.5"
                    case "mic":     return "M 12 4 A 3 3 0 0 1 15 7 V 12 A 3 3 0 0 1 9 12 V 7 "
                        + "A 3 3 0 0 1 12 4 M 6 12 A 6 6 0 0 0 18 12 M 12 18 V 21"
                    case "attach":  return "M 17 8 L 9.5 15.5 A 3 3 0 0 0 13.5 19.5 L 20 13 "
                                         + "A 5.5 5.5 0 0 0 12.5 5.5 L 6 12 "
                                         + "A 8 8 0 0 0 17.5 23"
                    case "bookmark": return "M 7 4 H 17 V 20 L 12 15.5 L 7 20 Z"
                    case "pin":     return "M 9 3 L 15 3 L 14 10 L 18 13 L 6 13 L 10 10 Z M 12 13 L 12 21"
                    case "copy":    return "M 9 9 L 20 9 L 20 20 L 9 20 Z M 5 15 L 4 15 L 4 4 L 15 4 L 15 5"
                    case "cursor":  return "M 9 4 L 15 4 M 12 4 L 12 20 M 9 20 L 15 20"
                    case "edit":    return "M 4 20 L 4 16 L 16 4 L 20 8 L 8 20 Z M 14 6 L 18 10"
                    case "stop":    return "M 7 7 L 17 7 L 17 17 L 7 17 Z"
                    case "burger":  return "M 4 7 L 20 7 M 4 12 L 20 12 M 4 17 L 20 17"
                    case "person":  return "M 12 4 A 3.7 3.7 0 1 1 11.99 4 "
                                         + "M 5 20 A 7 7 0 0 1 19 20"
                    case "send":    return "M 3 12 L 21 4 L 14 21 L 11.5 13.5 Z M 11.5 13.5 L 21 4"
                    case "play":    return "M 8 5 L 19 12 L 8 19 Z"
                    case "file":    return "M 6 3 H 14 L 19 8 V 21 H 6 Z M 14 3 V 8 H 19"
                    case "save":    return "M 12 4 V 15 M 8 11 L 12 15 L 16 11 "
                                         + "M 5 17 V 20 H 19 V 17"
                    case "reply":   return "M 10 5 L 4 10 L 10 15 M 4 10 H 14 "
                                         + "A 5 5 0 0 1 19 15 V 19"
                    case "trash":   return "M 5 7 H 19 M 10 7 V 5 H 14 V 7 "
                                         + "M 7 7 L 8 20 H 16 L 17 7 M 10 11 V 17 M 14 11 V 17"
                    case "smile":   return "M 12 3 A 9 9 0 1 1 11.99 3 "
                                         + "M 8 14 A 5 5 0 0 0 16 14 M 9 9 V 10 M 15 9 V 10"
                    case "key":     return "M 18.93 8.25 A 3.18 3.18 0 1 0 12.57 8.25 A 3.18 3.18 0 1 0 18.93 8.25 Z "
                                         + "M 13.5 10.5 L 4.5 19.5 M 6.5 17.5 L 8 19 M 9 15 L 10.5 16.5"
                    case "lock":    return "M 7 11 H 17 V 20 H 7 Z M 9.5 11 V 8 A 2.5 2.5 0 0 1 14.5 8 V 11"
                    case "disk":    return "M 12 4 C 7 4 4 5.3 4 7 C 4 8.7 7 10 12 10 "
                                         + "C 17 10 20 8.7 20 7 C 20 5.3 17 4 12 4 Z "
                                         + "M 4 7 V 17 C 4 18.7 7 20 12 20 C 17 20 20 18.7 20 17 V 7 "
                                         + "M 4 12 C 4 13.7 7 15 12 15 C 17 15 20 13.7 20 12"
                    case "server":  return "M 4 5 H 20 V 10 H 4 Z M 4 14 H 20 V 19 H 4 Z "
                                         + "M 7 7.5 V 7.6 M 7 16.5 V 16.6"
                    case "list":    return "M 5 7 H 5.1 M 5 12 H 5.1 M 5 17 H 5.1 "
                                         + "M 9 7 H 19 M 9 12 H 19 M 9 17 H 19"
                    case "devices": return "M 3 5 H 14 V 13 H 3 Z M 6 16 H 11 M 8.5 13 V 16 "
                                         + "M 16 9 H 21 V 20 H 16 Z"
                    case "broadcast": return "M 12 12 V 12.1 "
                                         + "M 8.8 8.8 A 4.5 4.5 0 0 0 8.8 15.2 "
                                         + "M 15.2 8.8 A 4.5 4.5 0 0 1 15.2 15.2 "
                                         + "M 5.6 5.6 A 9 9 0 0 0 5.6 18.4 "
                                         + "M 18.4 5.6 A 9 9 0 0 1 18.4 18.4"
                    case "qr":      return "M 4 4 H 9.5 V 9.5 H 4 Z M 14.5 4 H 20 V 9.5 H 14.5 Z "
                                         + "M 4 14.5 H 9.5 V 20 H 4 Z "
                                         + "M 14.5 14.5 V 14.6 M 20 14.5 V 14.6 M 17.2 17.2 V 17.3 "
                                         + "M 14.5 20 V 20.1 M 20 20 V 20.1"
                    case "camera":  return "M 3 8 H 7.5 L 9.5 5.5 H 14.5 L 16.5 8 H 21 V 19 H 3 Z "
                                         + "M 12 13 A 3.3 3.3 0 1 1 11.99 13"
                    case "folder":  return "M 3 19 V 5 H 10 L 12 8 H 21 V 19 Z"
                    case "block":   return "M 12 3 A 9 9 0 1 1 11.99 3 M 5.6 18.4 L 18.4 5.6"
                    case "people":  return "M 9 5 A 3.2 3.2 0 1 1 8.99 5 M 2.5 19 A 6.5 6.5 0 0 1 15.5 19 "
                                         + "M 16 6.4 A 3.2 3.2 0 0 1 16 11.6 M 17.5 14.5 A 6.5 6.5 0 0 1 21.5 19"
                    case "power":   return "M 12 3.5 V 11 M 7.05 7.05 A 7 7 0 1 0 16.95 7.05"
                    case "signout": return "M 13 4 H 4 V 20 H 13 M 13.5 12 H 20 "
                                         + "M 16.5 8.5 L 20 12 L 16.5 15.5"
                    default:        return ""
                    }
                }
            }
        }

        ShapePath {
            strokeColor: "transparent"
            fillColor: root.name === "more" || root.name === "dot" || root.name === "gear"
                ? root.color
                : "transparent"
            fillRule: ShapePath.OddEvenFill
            PathSvg {
                path: {
                    switch (root.name) {
                    case "more": return "M 12 4 A 1.6 1.6 0 1 1 11.99 4 Z "
                                      + "M 12 10.4 A 1.6 1.6 0 1 1 11.99 10.4 Z "
                                      + "M 12 16.8 A 1.6 1.6 0 1 1 11.99 16.8 Z"
                    case "dot":  return "M 12 8 A 4 4 0 1 1 11.99 8 Z"
                    case "gear": return "M 18.5 9.5 L 21.4 10.0 L 21.4 14.0 L 18.5 14.5 "
                                      + "L 18.4 14.8 L 20.1 17.2 L 17.2 20.1 L 14.8 18.4 "
                                      + "L 14.5 18.5 L 14.0 21.4 L 10.0 21.4 L 9.5 18.5 "
                                      + "L 9.2 18.4 L 6.8 20.1 L 3.9 17.2 L 5.6 14.8 "
                                      + "L 5.5 14.5 L 2.6 14.0 L 2.6 10.0 L 5.5 9.5 "
                                      + "L 5.6 9.2 L 3.9 6.8 L 6.8 3.9 L 9.2 5.6 "
                                      + "L 9.5 5.5 L 10.0 2.6 L 14.0 2.6 L 14.5 5.5 "
                                      + "L 14.8 5.6 L 17.2 3.9 L 20.1 6.8 L 18.4 9.2 Z "
                                      + "M 15.1 12 A 3.1 3.1 0 1 0 8.9 12 "
                                      + "A 3.1 3.1 0 1 0 15.1 12 Z"
                    default:     return ""
                    }
                }
            }
        }
    }
}
