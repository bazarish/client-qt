// Bazarish project (c) 2026
#pragma once

namespace bazarish::app {

// Delivery status of a message, stored as StoredMessage.status. The values
// mirror the QML `DeliveryStatus` enum (app/qml/DeliveryStatus.qml) one-to-one
// - keep them in sync.
namespace DeliveryStatus {
enum Value {
    Preparing = 0,          // hollow ring: making the address it leaves from
    Delivering = 1,         // grey: on its way to the recipient's server
    AtRecipientServer = 2,  // yellow: handed off to the recipient's server
    Delivered = 3,          // green: the recipient's client confirmed receipt
    Failed = 4,             // red: delivery failed
    Received = 5,           // an incoming message (no indicator shown)
};
}  // namespace DeliveryStatus

}  // namespace bazarish::app
