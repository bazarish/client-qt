// Bazarish project (c) 2026
import QtQuick
import QtQuick.Controls as Controls
import Bazarish

// Qt's field, with its touches handed to the app's selection menu on a phone.
Controls.TextField {
    id: field
    PhoneSelection { field: field }
}
