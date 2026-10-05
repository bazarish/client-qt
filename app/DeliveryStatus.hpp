// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QtQmlIntegration>

namespace bazarish::app {

namespace DeliveryStatus {
Q_NAMESPACE
QML_ELEMENT

enum Value {
    Preparing = 0,
    Delivering = 1,
    AtRecipientServer = 2,
    Delivered = 3,
    Failed = 4,
    Received = 5,
};
Q_ENUM_NS(Value)

}  // namespace DeliveryStatus

}  // namespace bazarish::app
