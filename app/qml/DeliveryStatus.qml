import QtQuick

// Delivery status values for the message indicator, referenced as
// DeliveryStatus.AtSenderServer etc. Mirrors the C++ enum in
// app/DeliveryStatus.hpp one-to-one (auto-numbered 0..5) - keep in sync.
QtObject {
    enum Value {
        Sending,            // in flight to our own server (hollow ring)
        AtSenderServer,     // grey: our own server accepted the envelope
        AtRecipientServer,  // yellow: handed off to the recipient's server
        Delivered,          // green: the recipient's client confirmed receipt
        Failed,             // red: delivery failed
        Received            // an incoming message (no indicator shown)
    }
}
