// Bazarish project (c) 2026
#pragma once

#include <TranscriptDb.hpp>

#include <QObject>
#include <QtQmlIntegration>

namespace bazarish::app {

namespace DeliveryStatus {
Q_NAMESPACE
QML_ELEMENT

enum Value {
    Preparing = static_cast<int>(client::DeliveryState::ePreparing),
    Delivering = static_cast<int>(client::DeliveryState::eDelivering),
    AtRecipientServer = static_cast<int>(client::DeliveryState::eAtRecipientServer),
    Delivered = static_cast<int>(client::DeliveryState::eDelivered),
    Failed = static_cast<int>(client::DeliveryState::eFailed),
    Received = static_cast<int>(client::DeliveryState::eReceived),
};
Q_ENUM_NS(Value)

}  // namespace DeliveryStatus

}  // namespace bazarish::app
