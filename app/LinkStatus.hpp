// Bazarish project (c) 2026
#pragma once

#include <LinkFailure.hpp>

#include <QObject>
#include <QtQmlIntegration>

namespace bazarish::app {

namespace LinkStatus {
Q_NAMESPACE
QML_ELEMENT

enum Value {
    None = static_cast<int>(client::LinkFailure::eNone),
    NoNetwork = static_cast<int>(client::LinkFailure::eNoNetwork),
    I2pUnreachable = static_cast<int>(client::LinkFailure::eI2pUnreachable),
    GatewayUnreachable = static_cast<int>(client::LinkFailure::eGatewayUnreachable),
    GatewayRefused = static_cast<int>(client::LinkFailure::eGatewayRefused),
    TunnelsBuilding = static_cast<int>(client::LinkFailure::eTunnelsBuilding),
    ServerUnreachable = static_cast<int>(client::LinkFailure::eServerUnreachable),
    ServerDown = static_cast<int>(client::LinkFailure::eServerDown),
    ServerRefused = static_cast<int>(client::LinkFailure::eServerRefused),
};
Q_ENUM_NS(Value)

}  // namespace LinkStatus

}  // namespace bazarish::app
