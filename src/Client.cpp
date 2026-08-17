// Bazarish project (c) 2026
#include "Client.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/I2pAddress.hpp>
#include <bazarish/Resolve.hpp>

#include <nlohmann/json.hpp>

namespace bazarish::client {

Bytes sealDeliveryEnvelope(const std::string& deliveryClass, const std::string& mailbox,
    const std::string& messageId, const std::optional<Bytes>& token,
    const Key& recipientSealingKey)
{
    nlohmann::json inner = {
        {"class", deliveryClass},
        {"mailbox", mailbox},
        {"messageId", messageId},
    };
    if (token.has_value()) {
        inner["token"] = toBase64(*token);
    }
    const std::string text = inner.dump();
    return cms::seal(Bytes(text.begin(), text.end()), recipientSealingKey);
}

Client::Client(Identity identity, std::string clientId, ServerEndpoint endpoint,
    std::filesystem::path i2pDataDir)
    : identity_(std::move(identity))
    , api_(identity_, std::move(clientId), std::move(endpoint), std::move(i2pDataDir))
{
}

const Identity& Client::identity() const
{
    return identity_;
}

const std::string& Client::clientId() const
{
    return api_.clientId();
}

const ServerEndpoint& Client::endpoint() const
{
    return api_.endpoint();
}

std::string Client::activeFacadeUrl() const
{
    return api_.activeFacadeUrl();
}

bool Client::activeFacadeIsI2p() const
{
    return api_.activeFacadeIsI2p();
}

SubscribeResult Client::submitSubscription(const std::string& path,
    const std::int64_t issuedAt, const std::int64_t notAfter, const Bytes& sealingPrekeyDer,
    const std::string& ownDest)
{
    // Ask the messaging server which destination + serving sealing key it has
    // assigned us, then fold them into the user-signed certificate (our contact
    // card) alongside the sealing prekey. The server fingerprint is kept as the
    // lifecycle anchor the service node checks, but routing is by destination.
    // While the destination is still building the server reports no address yet,
    // so the card carries ours: the destination is ours, and its address is the
    // master b32 we already hold.
    const DestinationInfo destination = myDestination();
    const std::string dest = destination.dest.empty() ? ownDest : destination.dest;
    const Bytes cert
        = SubscriptionCertificate::issue(identity_, api_.endpoint().serverFingerprint, issuedAt,
            notAfter, sealingPrekeyDer, dest, destination.servingSealingKeyDer);
    const ApiResponse response = api_.postJson(path, {{"cert", toBase64(cert)}});
    const nlohmann::json body = response.json();

    SubscribeResult result;
    result.subscriptionCertDer = cert;
    result.notAfter = body.at("notAfter").get<std::int64_t>();
    result.quotaBytes = body.at("quotaBytes").get<std::uint64_t>();
    result.maxTermSeconds = body.at("maxTermSeconds").get<std::int64_t>();
    result.dest = dest;
    result.servingSealingKeyDer = destination.servingSealingKeyDer;
    return result;
}

SubscribeResult Client::subscribe(const std::int64_t issuedAt, const std::int64_t notAfter,
    const Bytes& sealingPrekeyDer, const std::string& ownDest)
{
    return submitSubscription(
        "/v1/account/subscribe", issuedAt, notAfter, sealingPrekeyDer, ownDest);
}

SubscribeResult Client::renew(const std::int64_t issuedAt, const std::int64_t notAfter,
    const Bytes& sealingPrekeyDer, const std::string& ownDest)
{
    return submitSubscription("/v1/account/renew", issuedAt, notAfter, sealingPrekeyDer, ownDest);
}

Subscription Client::subscriptionStatus()
{
    const ApiResponse response = api_.get("/v1/account/subscription");
    const nlohmann::json body = response.json();
    Subscription result;
    result.notAfter = body.at("notAfter").get<std::int64_t>();
    result.quotaBytes = body.at("quotaBytes").get<std::uint64_t>();
    return result;
}

void Client::unsubscribe()
{
    api_.del("/v1/account/subscription");
}

PortalInfo Client::fetchPortalInfo()
{
    const ApiResponse response = api_.get("/v1/account/portal");
    const nlohmann::json body = response.json();
    PortalInfo info;
    info.message = body.value("message", std::string());
    if (const auto links = body.find("links"); links != body.end() && links->is_array()) {
        for (const nlohmann::json& link : *links) {
            if (link.is_string()) {
                info.links.push_back(link.get<std::string>());
            }
        }
    }
    return info;
}

void Client::sendI2pTransient(const std::string& transientB64, const std::int64_t expiresUnix)
{
    // Raises on refusal (a moderated server withholds the destination until an
    // operator approves the account), which the caller must not hide: without a
    // delegation the user has no routing at all.
    api_.postJson(
        "/v1/account/i2p-dest", {{"transient", transientB64}, {"expiresUnix", expiresUnix}});
}

I2pDestStatus Client::i2pStatus()
{
    const ApiResponse response = api_.get("/v1/account/i2p-status");
    const nlohmann::json body = response.json();
    I2pDestStatus status;
    status.approval = body.value("approval", std::string());
    status.registrationMessage = body.value("registrationMessage", std::string());
    status.transientExpires = body.value("transientExpires", std::int64_t{0});
    status.transientUpdatedAt = body.value("transientUpdatedAt", std::int64_t{0});
    return status;
}

StorageUsage Client::storageUsage()
{
    StorageUsage usage;
    // Each backend is polled independently so one being offline does not hide the
    // other. Both ride the same facade (server-core for /v1/messaging/*, blob
    // storage for /v1/storage/*).
    try {
        const nlohmann::json body = api_.get("/v1/messaging/storage-usage").json();
        usage.mailboxUsedBytes = body.value("usedBytes", std::uint64_t{0});
        usage.mailboxQuotaBytes = body.value("quotaBytes", std::uint64_t{0});
        usage.mailboxOk = true;
    } catch (const std::exception&) {
        // Unreachable / unauthorized: leave the mailbox half stale (ok=false).
    }
    return usage;
}

ContactInfo Client::lookupContact(const std::string& peerFingerprint)
{
    const ApiResponse response
        = api_.getPublic("/v1/account/contact", "user=" + peerFingerprint);
    const nlohmann::json body = response.json();
    ContactInfo info;
    info.subscriptionCert = SubscriptionCertificate::verify(
        fromBase64(body.at("subscriptionCert").get<std::string>()));
    return info;
}

FetchOutcome Client::relayFetch(
    const std::string& toDest, const std::string& op, const Bytes& sealed)
{
    const ApiResponse response = api_.postJson("/v1/messaging/fetch",
        {
            {"toDest", toDest},
            {"op", op},
            {"sealed", toBase64(sealed)},
        },
        ApiClient::kFetchReadTimeoutSeconds);
    const nlohmann::json body = response.json();
    FetchOutcome outcome;
    outcome.ok = body.at("ok").get<bool>();
    if (body.contains("sealed")) {
        outcome.sealed = fromBase64(body.at("sealed").get<std::string>());
    }
    outcome.errorCode = body.value("errorCode", std::string());
    return outcome;
}

ContactInfo Client::fetchCard(const Descriptor& descriptor, const FetchTransport& transport)
{
    // Seal the query (which fingerprint) to the serving server's serving sealing
    // key so a relay cannot read it; the response comes back sealed to a fresh
    // ephemeral key only we hold.
    const Key ephemeral = Key::generateSealing();
    const CardFetchQuery query{descriptor.fingerprint, ephemeral.publicDer()};
    const std::string queryJson = toJson(query).dump();
    const Bytes sealedQuery = cms::seal(
        Bytes(queryJson.begin(), queryJson.end()), Key::fromPublicDer(descriptor.srvKeyDer));

    const FetchOutcome outcome = transport(descriptor.srv, "card", sealedQuery);
    if (!outcome.ok) {
        throw std::runtime_error("card fetch failed: "
            + (outcome.errorCode.empty() ? std::string("CARD_UNKNOWN") : outcome.errorCode));
    }
    const Bytes responseBytes = cms::unseal(outcome.sealed, ephemeral);
    const CardFetchResponse fetched
        = cardFetchResponseFromJson(nlohmann::json::parse(responseBytes));

    ContactInfo info;
    info.subscriptionCert = SubscriptionCertificate::verify(fetched.subscriptionCertDer);
    // The fingerprint is the trust anchor: the card is user-signed, so a wrong
    // server can only withhold, never forge a card for someone else's fingerprint.
    if (info.subscriptionCert.user != descriptor.fingerprint) {
        throw std::runtime_error("fetched card is for a different fingerprint");
    }
    validateB32I2pHost(info.subscriptionCert.dest);
    return info;
}

Descriptor Client::resolveAlias(const std::string& alias, const ResolverCoordinate& resolver,
    const std::int64_t now, const FetchTransport& transport)
{
    // Seal the query (which alias) to the resolver's serving key so a relay on
    // the proxy path cannot read it; the response is sealed to a fresh ephemeral
    // key only we hold.
    const Key ephemeral = Key::generateSealing();
    const ResolveQuery query{alias, ephemeral.publicDer()};
    const std::string queryJson = toJson(query).dump();
    const Bytes sealedQuery = cms::seal(
        Bytes(queryJson.begin(), queryJson.end()), Key::fromPublicDer(resolver.sealingKeyDer));

    const FetchOutcome outcome = transport(resolver.dest, "resolve", sealedQuery);
    if (!outcome.ok) {
        throw std::runtime_error("alias resolve failed: "
            + (outcome.errorCode.empty() ? std::string("ALIAS_UNKNOWN") : outcome.errorCode));
    }
    const Bytes responseBytes = cms::unseal(outcome.sealed, ephemeral);
    const ResolveResponse fetched
        = resolveResponseFromJson(nlohmann::json::parse(responseBytes));

    // Verify the signature chain (record -> delegated key -> hardcoded root) and
    // that the record is for the alias we asked for. This is the integrity
    // anchor: even a malicious relay can only withhold, never forge a binding.
    const ResolveRecord record = verifyResolveRecord(
        fetched.recordDer, fetched.delegationDer, resolver.rootFingerprint, now);
    if (record.alias != alias) {
        throw std::runtime_error("resolver returned a record for a different alias");
    }
    return record.descriptor;
}

DestinationInfo Client::myDestination()
{
    const ApiResponse response = api_.get("/v1/messaging/destination");
    const nlohmann::json body = response.json();
    DestinationInfo info;
    info.dest = body.at("dest").get<std::string>();
    info.state = body.value("state", std::string());
    // The dest is empty while a personal destination is still building or has
    // gone offline; only validate (and later publish) a present address.
    if (!info.dest.empty()) {
        validateB32I2pHost(info.dest);
    }
    const std::string servingKey = body.value("servingKey", std::string());
    if (!servingKey.empty()) {
        info.servingSealingKeyDer = fromBase64(servingKey);
    }
    return info;
}

void Client::registerThisClient()
{
    api_.postJson("/v1/messaging/clients", {{"clientId", api_.clientId()}});
}

void Client::retireClient(const std::string& clientId)
{
    api_.del("/v1/messaging/clients/" + clientId);
}

void Client::registerTokenHashes(const std::vector<Bytes>& hashes)
{
    nlohmann::json encoded = nlohmann::json::array();
    for (const Bytes& hash : hashes) {
        encoded.push_back(toBase64(hash));
    }
    api_.postJson("/v1/messaging/tokens", {{"hashes", encoded}});
}

void Client::deleteTokenHashes(const std::vector<Bytes>& hashes)
{
    nlohmann::json encoded = nlohmann::json::array();
    for (const Bytes& hash : hashes) {
        encoded.push_back(toBase64(hash));
    }
    api_.del("/v1/messaging/tokens", {{"hashes", encoded}});
}

std::vector<PendingEntry> Client::listPending()
{
    const ApiResponse response = api_.get("/v1/messaging/pending");
    const nlohmann::json body = response.json();

    std::vector<PendingEntry> entries;
    for (const nlohmann::json& entry : body.at("pending")) {
        entries.push_back(
            {entry.at("id").get<std::string>(), entry.at("class").get<std::string>()});
    }
    return entries;
}

Bytes Client::fetchBlob(const std::string& blobId)
{
    const ApiResponse response = api_.get("/v1/messaging/pending/" + blobId);
    return response.body;
}

std::vector<Bytes> Client::fetchReseed()
{
    // Clearnet only: this is what bootstraps the I2P transport (see getClearnet).
    const nlohmann::json body = api_.getClearnet("/v1/messaging/reseed").json();
    std::vector<Bytes> routers;
    for (const nlohmann::json& entry : body.at("routers")) {
        routers.push_back(fromBase64(entry.get<std::string>()));
    }
    return routers;
}

void Client::ack(const std::string& blobId)
{
    api_.postJson("/v1/messaging/ack", {{"blobId", blobId}});
}

std::string Client::submitSend(
    const std::string& toDest, const Bytes& sealed, const Bytes& payload, const std::string& messageId)
{
    const ApiResponse response = api_.postJson("/v1/messaging/send",
        {
            {"toDest", toDest},
            {"sealed", toBase64(sealed)},
            {"payload", toBase64(payload)},
            {"messageId", messageId},
        });
    return response.json().at("attemptId").get<std::string>();
}

SendStatus Client::pollSend(const std::string& attemptId)
{
    const ApiResponse response = api_.get("/v1/messaging/send/" + attemptId);
    const nlohmann::json body = response.json();

    SendStatus result;
    result.status = body.at("status").get<std::string>();
    result.phase = body.value("phase", std::string());
    if (body.contains("error")) {
        const nlohmann::json& error = body.at("error");
        result.errorCode = errorCodeFromString(error.at("code").get<std::string>());
        result.errorMessage = error.at("message").get<std::string>();
    }
    return result;
}





}  // namespace bazarish::client
