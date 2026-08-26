import QtQuick

// Delivery status values for the message indicator, referenced as
// DeliveryStatus.Delivering etc. Mirrors the C++ enum in
// app/DeliveryStatus.hpp one-to-one (auto-numbered 0..5) - keep in sync.
QtObject {
    enum Value {
        Preparing,          // hollow ring: making the address it leaves from
        Delivering,         // grey: on its way to the recipient's server
        AtRecipientServer,  // yellow: handed off to the recipient's server
        Delivered,          // green: the recipient's client confirmed receipt
        Failed,             // red: delivery failed
        Received            // an incoming message (no indicator shown)
    }
}
