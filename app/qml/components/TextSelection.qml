// Bazarish project (c) 2026
pragma Singleton
import QtQuick

QtObject {
    signal tapped(Item field, point at)
    signal longPressed(Item field, point at)
}
