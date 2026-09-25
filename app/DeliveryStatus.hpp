// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QtQmlIntegration>

namespace bazarish::app {

// Delivery status of a message, stored as StoredMessage.status and read by the
// bubble to pick its indicator. Registered for QML so both sides read one
// definition rather than two that have to be kept level.
namespace DeliveryStatus {
Q_NAMESPACE
QML_ELEMENT

enum Value {
    Preparing = 0,          // hollow ring: making the address it leaves from
    Delivering = 1,         // grey: on its way to the recipient's server
    AtRecipientServer = 2,  // yellow: handed off to the recipient's server
    Delivered = 3,          // green: the recipient's client confirmed receipt
    Failed = 4,             // red: delivery failed
    Received = 5,           // an incoming message (no indicator shown)
};
Q_ENUM_NS(Value)

}  // namespace DeliveryStatus

}  // namespace bazarish::app
