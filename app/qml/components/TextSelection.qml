// Bazarish project (c) 2026
pragma Singleton
import QtQuick

// Where a phone's text fields report the touches the app's selection menu answers.
QtObject {
    signal tapped(Item field, point at)
    signal longPressed(Item field, point at)
}
