// Bazarish project (c) 2026
#include "LinkFailureText.hpp"

#include "AccountEvents.hpp"

#include <ApiClient.hpp>
#include <LinkFailure.hpp>

#include <QObject>

#include <optional>
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
    case FarSide::eContactServer:
        return QObject::tr("The contact's server is not answering");
    case FarSide::eContact:
        return QObject::tr("The contact is not answering");
    case FarSide::eDevice:
        return QObject::tr("The other device is not answering");
    }
    throw std::logic_error("no far side to name");
}

QString notFound(const FarSide side)
{
    switch (side) {
    case FarSide::eServer:
        return QObject::tr("The server's address is not found in I2P yet");
    case FarSide::eSite:
        return QObject::tr("The site's address is not found in I2P yet");
    case FarSide::eRegistry:
        return QObject::tr("The alias registry's address is not found in I2P yet");
    case FarSide::eContactServer:
        return QObject::tr("The contact's server is not found in I2P yet");
    case FarSide::eContact:
        return QObject::tr("The contact is not found in I2P yet");
    case FarSide::eDevice:
        return QObject::tr("The other device is not found in I2P yet");
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
    case FarSide::eContactServer:
        return QObject::tr("This invite is out of date");
    case FarSide::eContact:
        return QObject::tr("The contact refused the transfer");
    case FarSide::eDevice:
        return QObject::tr("The other device refused the pairing");
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
    case FarSide::eContactServer:
        return QObject::tr("The contact's server answered incorrectly");
    case FarSide::eContact:
        return QObject::tr("The file arrived damaged");
    case FarSide::eDevice:
        return QObject::tr("The other device answered incorrectly");
    }
    throw std::logic_error("no far side to name");
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
    case client::LinkFailure::eAddressNotFound:
        return notFound(side);
    }
    throw std::logic_error("no words for this link failure");
}

QString localFailureText(const client::LocalFailure failure)
{
    switch (failure) {
    case client::LocalFailure::eNone:
        return {};
    case client::LocalFailure::eInvalidInvite:
        return QObject::tr("This invite link is damaged");
    case client::LocalFailure::eInvalidAlias:
        return QObject::tr("This alias is not valid");
    case client::LocalFailure::eInvalidPairLink:
        return QObject::tr("This pairing link is damaged");
    case client::LocalFailure::eNoRegistry:
        return QObject::tr("This build has no alias registry");
    case client::LocalFailure::eAccountTooLarge:
        return QObject::tr("The account is too large to move to another device");
    case client::LocalFailure::ePairingBusy:
        return QObject::tr("Another device is already being added");
    case client::LocalFailure::eStopped:
        return QObject::tr("The transfer was stopped");
    case client::LocalFailure::eStoppedThere:
        return QObject::tr("The contact stopped the transfer");
    case client::LocalFailure::eFileGone:
        return QObject::tr("The contact no longer has this file");
    case client::LocalFailure::eFileUnreadable:
        return QObject::tr("The file cannot be read");
    case client::LocalFailure::eFileUnwritable:
        return QObject::tr("The file cannot be saved");
    case client::LocalFailure::eInternal:
        return QObject::tr("Internal error");
    }
    throw std::logic_error("no words for this local failure");
}

QString causeText(const client::FailureCause& cause, const FarSide side)
{
    if (cause.link != client::LinkFailure::eNone) {
        return linkFailureText(static_cast<int>(cause.link), side);
    }
    return localFailureText(cause.local);
}

QString failureText(const std::exception& error, const FarSide side)
{
    // The server's own word outranks how its answer travelled.
    if (const auto* const answer = dynamic_cast<const client::ApiError*>(&error);
        answer != nullptr && answer->code.has_value()) {
        return QString::fromUtf8(error.what());
    }
    const std::optional<client::FailureCause> cause = client::carriedCause(error);
    if (!cause.has_value()) {
        return QString::fromUtf8(error.what());
    }
    return causeText(*cause, side);
}

QString notStoredText(const client::NotStored reason)
{
    switch (reason) {
    case client::NotStored::eRefused:
        throw std::logic_error("a refusal is named by its server's code");
    case client::NotStored::eWithdrawn:
        return QObject::tr("The recipient was removed before delivery");
    case client::NotStored::eExpired:
        return QObject::tr("The message went out of date before delivery");
    case client::NotStored::eNotSealed:
        return QObject::tr("The message could not be encrypted for the recipient");
    }
    throw std::logic_error("no words for this outcome");
}

QString samProbeText(const sam::ProbeFailure failure)
{
    switch (failure) {
    case sam::ProbeFailure::eBadAddress:
        return QObject::tr("The router address must be a local IPv4 address");
    case sam::ProbeFailure::eNoRouter:
        return QObject::tr("No router answers at this address");
    case sam::ProbeFailure::eRefused:
        return QObject::tr("The router refused the SAM connection");
    case sam::ProbeFailure::eMalformed:
        return QObject::tr("The router answered incorrectly");
    }
    throw std::logic_error("no words for this router check");
}

}  // namespace bazarish::app
