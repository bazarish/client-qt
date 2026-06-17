// Bazarish project (c) 2026
#include "Client.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/I2pAddress.hpp>

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

Client::Client(Identity identity, std::string clientId, ServerEndpoint endpoint)
    : identity_(std::move(identity))
    , api_(identity_, std::move(clientId), std::move(endpoint))
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

SubscribeResult Client::submitSubscription(const std::string& path,
    const std::int64_t issuedAt, const std::int64_t notAfter, const Bytes& sealingPrekeyDer)
{
    // Ask the messaging server which destination + serving sealing key it has
    // assigned us, then fold them into the user-signed certificate (our contact
    // card) alongside the sealing prekey. The server fingerprint is kept as the
    // lifecycle anchor the service node checks, but routing is by destination.
    const DestinationInfo destination = myDestination();
    const Bytes cert
        = SubscriptionCertificate::issue(identity_, api_.endpoint().serverFingerprint, issuedAt,
            notAfter, sealingPrekeyDer, destination.dest, destination.servingSealingKeyDer);
    const ApiResponse response = api_.postJson(path, {{"cert", toBase64(cert)}});
    const nlohmann::json body = response.json();

    SubscribeResult result;
    result.subscriptionCertDer = cert;
    result.notAfter = body.at("notAfter").get<std::int64_t>();
    result.quotaBytes = body.at("quotaBytes").get<std::uint64_t>();
    result.maxTermSeconds = body.at("maxTermSeconds").get<std::int64_t>();
    result.dest = destination.dest;
    result.servingSealingKeyDer = destination.servingSealingKeyDer;
    return result;
}

SubscribeResult Client::subscribe(
    const std::int64_t issuedAt, const std::int64_t notAfter, const Bytes& sealingPrekeyDer)
{
    return submitSubscription("/v1/account/subscribe", issuedAt, notAfter, sealingPrekeyDer);
}

SubscribeResult Client::renew(
    const std::int64_t issuedAt, const std::int64_t notAfter, const Bytes& sealingPrekeyDer)
{
    return submitSubscription("/v1/account/renew", issuedAt, notAfter, sealingPrekeyDer);
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

bool Client::sendI2pTransient(const std::string& transientB64, const std::int64_t expiresUnix)
{
    const ApiResponse response = api_.postJson(
        "/v1/account/i2p-dest", {{"transient", transientB64}, {"expiresUnix", expiresUnix}});
    return response.status == 200;
}

void Client::registerAlias(const std::string& alias, const std::int64_t issuedAt,
    const std::optional<std::int64_t> notAfter)
{
    const Bytes cert = AliasCertificate::issue(identity_, alias, issuedAt, notAfter);
    api_.postJson("/v1/account/alias", {{"cert", toBase64(cert)}});
}

void Client::releaseAlias(const std::string& alias)
{
    api_.del("/v1/account/alias", {{"alias", alias}});
}

ResolveResult Client::resolve(const std::string& alias)
{
    const ApiResponse response = api_.getPublic("/v1/account/resolve", "alias=" + alias);
    const nlohmann::json body = response.json();

    ResolveResult result;
    result.user = body.at("user").get<std::string>();
    result.aliasCert
        = AliasCertificate::verify(fromBase64(body.at("aliasCert").get<std::string>()));
    result.subscriptionCert = SubscriptionCertificate::verify(
        fromBase64(body.at("subscriptionCert").get<std::string>()));
    return result;
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

DestinationInfo Client::myDestination()
{
    const ApiResponse response = api_.get("/v1/messaging/destination");
    const nlohmann::json body = response.json();
    DestinationInfo info;
    info.dest = body.at("dest").get<std::string>();
    validateB32I2pHost(info.dest);
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

void Client::ack(const std::string& blobId)
{
    api_.postJson("/v1/messaging/ack", {{"blobId", blobId}});
}

std::string Client::submitSend(
    const std::string& toDest, const Bytes& sealed, const Bytes& payload)
{
    const ApiResponse response = api_.postJson("/v1/messaging/send",
        {
            {"toDest", toDest},
            {"sealed", toBase64(sealed)},
            {"payload", toBase64(payload)},
        });
    return response.json().at("attemptId").get<std::string>();
}

SendStatus Client::pollSend(const std::string& attemptId)
{
    const ApiResponse response = api_.get("/v1/messaging/send/" + attemptId);
    const nlohmann::json body = response.json();

    SendStatus result;
    result.status = body.at("status").get<std::string>();
    if (body.contains("error")) {
        const nlohmann::json& error = body.at("error");
        result.errorCode = errorCodeFromString(error.at("code").get<std::string>());
        result.errorMessage = error.at("message").get<std::string>();
    }
    return result;
}

BlobUploadResult Client::uploadBlob(const PackedBlob& packed, const BlobRetention& retention)
{
    return bazarish::client::uploadBlob(api_, packed, retention);
}

BlobUploadResult Client::uploadBlobFromFile(
    const PackedBlobFile& packed, const BlobRetention& retention)
{
    return bazarish::client::uploadBlobFromFile(api_, packed, retention);
}

Bytes Client::fetchBlobViaProxy(const BlobPointer& pointer)
{
    return bazarish::client::fetchBlobViaProxy(api_, pointer);
}

void Client::deleteBlobViaProxy(const std::string& blobUrl, const std::string& deleteToken)
{
    bazarish::client::deleteBlobViaProxy(api_, blobUrl, deleteToken);
}

}  // namespace bazarish::client
