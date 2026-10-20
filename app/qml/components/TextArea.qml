// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls as Controls
import Bazarish

// Qt's text area, with its touches handed to the app's selection menu on a phone.
Controls.TextArea {
    id: field
    PhoneSelection { field: field }
}
