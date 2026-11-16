// Bazarish project (c) 2026
#include "LinkFailureText.hpp"

#include "AccountEvents.hpp"

#include <ApiClient.hpp>
#include <Client.hpp>
#include <LinkFailure.hpp>

#include <bazarish/I2p.hpp>

#include <QObject>

#include <stdexcept>

namespace bazarish::app {

namespace {

QString notAnswering(const FarSide side)
{
    switch (side) {
    case FarSide::eServer:
        return QObject::tr("The server is not answering");
    case FarSide::eSite:
        return QObject::tr("The site is not answering");
    case FarSide::eRegistry:
        return QObject::tr("The alias registry is not answering");
    }
    throw std::logic_error("no far side to name");
}

QString refused(const FarSide side)
{
    switch (side) {
    case FarSide::eServer:
        return QObject::tr("The server refused the connection");
    case FarSide::eSite:
        return QObject::tr("The site refused the sign-in");
    case FarSide::eRegistry:
        return QObject::tr("The alias registry refused the request");
    }
    throw std::logic_error("no far side to name");
}

QString answeredIncorrectly(const FarSide side)
{
    switch (side) {
    case FarSide::eServer:
        return QObject::tr("The server answered incorrectly");
    case FarSide::eSite:
        return QObject::tr("The site answered incorrectly");
    case FarSide::eRegistry:
        return QObject::tr("The alias registry answered incorrectly");
    }
    throw std::logic_error("no far side to name");
}

client::LinkFailure linkFailureOf(const client::FetchFailure failure)
{
    switch (failure) {
    case client::FetchFailure::eLeaseSetNotFound:
    case client::FetchFailure::eNotConnected:
    case client::FetchFailure::eBrokeAfterOpen:
        return client::LinkFailure::eServerUnreachable;
    case client::FetchFailure::eProtocol:
        return client::LinkFailure::eServerMalformed;
    }
    throw std::logic_error("no link failure for this fetch failure");
}

}  // namespace

QString linkFailureText(const int failure, const FarSide side)
{
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
        return notAnswering(side);
    case client::LinkFailure::eServerDown:
        return QObject::tr("No tunnel to the server");
    case client::LinkFailure::eServerRefused:
        return refused(side);
    case client::LinkFailure::eServerMalformed:
        return answeredIncorrectly(side);
    }
    throw std::logic_error("no words for this link failure");
}

QString failureText(const std::exception& error, const FarSide side)
{
    if (dynamic_cast<const bazarish::i2p::GatewayRefusal*>(&error) != nullptr) {
        return linkFailureText(static_cast<int>(client::LinkFailure::eGatewayRefused), side);
    }
    // The server's own word outranks how its answer travelled.
    if (const auto* const answer = dynamic_cast<const client::ApiError*>(&error);
        answer != nullptr && answer->code.has_value()) {
        return QString::fromUtf8(error.what());
    }
    if (const auto* const fetch = dynamic_cast<const client::FetchFailed*>(&error);
        fetch != nullptr) {
        return linkFailureText(static_cast<int>(linkFailureOf(fetch->failure())), side);
    }
    const auto* const link = dynamic_cast<const client::LinkError*>(&error);
    if (link == nullptr || link->failure == client::LinkFailure::eNone) {
        return QString::fromUtf8(error.what());
    }
    return linkFailureText(static_cast<int>(link->failure), side);
}

}  // namespace bazarish::app
