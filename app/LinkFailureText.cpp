// Bazarish project (c) 2026
#include "LinkFailureText.hpp"

#include "AccountEvents.hpp"

#include <LinkFailure.hpp>

#include <bazarish/I2p.hpp>

#include <QObject>

#include <stdexcept>

namespace bazarish::app {

QString linkFailureText(const int failure, const FarSide side)
{
    const bool site = side == FarSide::eSite;
    if (failure == kLinkRebuilding) {
        return QObject::tr("The links to the server are being rebuilt");
    }
    switch (static_cast<client::LinkFailure>(failure)) {
    case client::LinkFailure::eNone:
        return {};
    case client::LinkFailure::eNoNetwork:
        return QObject::tr("No network");
    case client::LinkFailure::eI2pUnreachable:
        return QObject::tr("No connection to the I2P network");
    case client::LinkFailure::eGatewayUnreachable:
        return QObject::tr("No connection to the external gateway");
    case client::LinkFailure::eGatewayRefused:
        return QObject::tr("The gateway refused a link to the server");
    case client::LinkFailure::eTunnelsBuilding:
        return QObject::tr("The I2P tunnels are being built");
    case client::LinkFailure::eServerUnreachable:
        return site ? QObject::tr("The site is not answering")
                    : QObject::tr("The server is not answering");
    case client::LinkFailure::eServerDown:
        return QObject::tr("No tunnel to the server");
    case client::LinkFailure::eServerRefused:
        return site ? QObject::tr("The site refused the sign-in")
                    : QObject::tr("The server refused the connection");
    case client::LinkFailure::eServerMalformed:
        return site ? QObject::tr("The site answered incorrectly")
                    : QObject::tr("The server answered incorrectly");
    }
    throw std::logic_error("no words for this link failure");
}

QString failureText(const std::exception& error, const FarSide side)
{
    if (dynamic_cast<const bazarish::i2p::GatewayRefusal*>(&error) != nullptr) {
        return linkFailureText(static_cast<int>(client::LinkFailure::eGatewayRefused), side);
    }
    const auto* const link = dynamic_cast<const client::LinkError*>(&error);
    if (link == nullptr || link->failure == client::LinkFailure::eNone) {
        return QString::fromUtf8(error.what());
    }
    return linkFailureText(static_cast<int>(link->failure), side);
}

}  // namespace bazarish::app
