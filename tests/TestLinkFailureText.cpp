// Bazarish project (c) 2026
#include "AccountEvents.hpp"
#include "LinkFailureText.hpp"
#include "TestUtil.hpp"

#include <ApiClient.hpp>
#include <Client.hpp>
#include <FailureCause.hpp>
#include <LinkFailure.hpp>
#include <OutboundCourier.hpp>

#include <bazarish/Errors.hpp>
#include <bazarish/I2p.hpp>

#include <stdexcept>

namespace {

using bazarish::app::FarSide;
using bazarish::app::causeText;
using bazarish::app::failureText;
using bazarish::app::linkFailureText;
using bazarish::app::localFailureText;
using bazarish::app::notStoredText;
using bazarish::app::samProbeText;
using bazarish::client::FailureCause;
using bazarish::client::ApiError;
using bazarish::client::FetchFailed;
using bazarish::client::FetchFailure;
using bazarish::client::LinkError;
using bazarish::client::LinkFailure;
using bazarish::client::LocalError;
using bazarish::client::LocalFailure;
using bazarish::client::NotStored;

constexpr int kHttpForbidden = 403;
constexpr int kHttpInsufficientStorage = 507;

QString textOf(const LinkFailure failure, const FarSide side)
{
    return linkFailureText(static_cast<int>(failure), side);
}

void everyFailureHasWordsOnEverySide()
{
    for (const FarSide side : {FarSide::eServer, FarSide::eSite, FarSide::eRegistry,
             FarSide::eContactServer, FarSide::eContact, FarSide::eDevice}) {
        CHECK(textOf(LinkFailure::eNone, side).isEmpty());
        CHECK(!linkFailureText(bazarish::app::kLinkRebuilding, side).isEmpty());
        for (const LinkFailure failure : {LinkFailure::eNoNetwork, LinkFailure::eI2pUnreachable,
                 LinkFailure::eGatewayUnreachable, LinkFailure::eGatewayRefused,
                 LinkFailure::eTunnelsBuilding, LinkFailure::eServerUnreachable,
                 LinkFailure::eServerDown, LinkFailure::eServerRefused,
                 LinkFailure::eServerMalformed}) {
            CHECK(!textOf(failure, side).isEmpty());
        }
    }
}

void theFarSideIsNamed()
{
    const LinkError silent(LinkFailure::eServerUnreachable, "no facade answered");
    CHECK(failureText(silent, FarSide::eServer) == QStringLiteral("The server is not answering"));
    CHECK(failureText(silent, FarSide::eSite) == QStringLiteral("The site is not answering"));
    CHECK(failureText(silent, FarSide::eRegistry)
        == QStringLiteral("The alias registry is not answering"));
    const LinkError garbled(LinkFailure::eServerMalformed, "the answer does not read");
    CHECK(failureText(garbled, FarSide::eRegistry)
        == QStringLiteral("The alias registry answered incorrectly"));
    const LinkError refused(LinkFailure::eServerRefused, "status 403");
    CHECK(failureText(refused, FarSide::eRegistry)
        == QStringLiteral("The alias registry refused the request"));
}

void aServerAnswerWithACodeKeepsItsWord()
{
    const ApiError full(bazarish::ErrorCode::eStorageFull, kHttpInsufficientStorage,
        LinkFailure::eServerRefused, "mailbox is full");
    CHECK(failureText(full, FarSide::eServer) == QStringLiteral("mailbox is full"));
    const ApiError answered(
        bazarish::ErrorCode::eDeviceRevoked, kHttpForbidden, LinkFailure::eNone, "revoked");
    CHECK(failureText(answered, FarSide::eServer) == QStringLiteral("revoked"));
}

void anAnswerWithoutACodeSaysHowItTravelled()
{
    const ApiError refused(std::nullopt, kHttpForbidden, LinkFailure::eServerRefused, "forbidden");
    CHECK(failureText(refused, FarSide::eServer)
        == QStringLiteral("The server refused the connection"));
    const ApiError garbled(std::nullopt, 0, LinkFailure::eServerMalformed, "not an answer");
    CHECK(failureText(garbled, FarSide::eServer)
        == QStringLiteral("The server answered incorrectly"));
    const ApiError offline(std::nullopt, 0, LinkFailure::eNoNetwork, "no network");
    CHECK(failureText(offline, FarSide::eServer) == QStringLiteral("No network"));
}

void aFailedFetchSaysWhetherTheFarSideWasSilent()
{
    for (const FetchFailure silent :
        {FetchFailure::eLeaseSetNotFound, FetchFailure::eNotConnected,
            FetchFailure::eBrokeAfterOpen}) {
        CHECK(failureText(FetchFailed(silent, "federation fetch: no answer"), FarSide::eRegistry)
            == QStringLiteral("The alias registry is not answering"));
    }
    CHECK(failureText(FetchFailed(FetchFailure::eProtocol, "does not read"), FarSide::eRegistry)
        == QStringLiteral("The alias registry answered incorrectly"));
}

void aLocalErrorKeepsItsOwnText()
{
    const std::runtime_error local("unknown contact: abc");
    CHECK(failureText(local, FarSide::eServer) == QStringLiteral("unknown contact: abc"));
    const LinkError answered(LinkFailure::eNone, "the answer's own word");
    CHECK(failureText(answered, FarSide::eServer) == QStringLiteral("the answer's own word"));
}

void everyLocalFailureHasWords()
{
    CHECK(localFailureText(LocalFailure::eNone).isEmpty());
    for (const LocalFailure failure : {LocalFailure::eInvalidInvite, LocalFailure::eInvalidAlias,
             LocalFailure::eInvalidPairLink, LocalFailure::eNoRegistry,
             LocalFailure::eAccountTooLarge, LocalFailure::ePairingBusy, LocalFailure::eStopped,
             LocalFailure::eStoppedThere, LocalFailure::eFileGone, LocalFailure::eFileUnreadable,
             LocalFailure::eFileUnwritable, LocalFailure::eInternal}) {
        CHECK(!localFailureText(failure).isEmpty());
    }
    for (const NotStored reason : {NotStored::eWithdrawn, NotStored::eExpired, NotStored::eNotSealed}) {
        CHECK(!notStoredText(reason).isEmpty());
    }
    for (const bazarish::sam::ProbeFailure failure :
        {bazarish::sam::ProbeFailure::eBadAddress, bazarish::sam::ProbeFailure::eNoRouter,
            bazarish::sam::ProbeFailure::eRefused, bazarish::sam::ProbeFailure::eMalformed}) {
        CHECK(!samProbeText(failure).isEmpty());
    }
}

void aCarriedCauseNamesItsSide()
{
    FailureCause silent;
    silent.link = LinkFailure::eServerUnreachable;
    CHECK(causeText(silent, FarSide::eContactServer)
        == QStringLiteral("The contact's server is not answering"));
    CHECK(causeText(silent, FarSide::eDevice) == QStringLiteral("The other device is not answering"));
    CHECK(causeText(silent, FarSide::eContact) == QStringLiteral("The contact is not answering"));
    FailureCause refused;
    refused.link = LinkFailure::eServerRefused;
    CHECK(causeText(refused, FarSide::eContactServer) == QStringLiteral("This invite is out of date"));
    FailureCause garbled;
    garbled.link = LinkFailure::eServerMalformed;
    CHECK(causeText(garbled, FarSide::eContact) == QStringLiteral("The file arrived damaged"));
    FailureCause local;
    local.local = LocalFailure::eNoRegistry;
    CHECK(causeText(local, FarSide::eRegistry) == QStringLiteral("This build has no alias registry"));
}

void aLocalErrorWithACauseIsNamed()
{
    const LocalError busy(LocalFailure::ePairingBusy, "this account is already waiting for a device");
    CHECK(failureText(busy, FarSide::eDevice)
        == QStringLiteral("Another device is already being added"));
}

void aGatewayRefusalNamesTheGateway()
{
    const bazarish::i2p::GatewayRefusal refusal("the gateway said no");
    CHECK(failureText(refusal, FarSide::eServer)
        == QStringLiteral("The gateway refused a link to the server"));
}

}  // namespace

int main()
try {
    everyFailureHasWordsOnEverySide();
    theFarSideIsNamed();
    aServerAnswerWithACodeKeepsItsWord();
    anAnswerWithoutACodeSaysHowItTravelled();
    aFailedFetchSaysWhetherTheFarSideWasSilent();
    aLocalErrorKeepsItsOwnText();
    aGatewayRefusalNamesTheGateway();
    everyLocalFailureHasWords();
    aCarriedCauseNamesItsSide();
    aLocalErrorWithACauseIsNamed();
    return 0;
} catch (...) {
    return testutil::failed();
}
