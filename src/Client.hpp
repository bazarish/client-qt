// Bazarish project (c) 2026
#pragma once

#include "ApiClient.hpp"
#include "BlobTransport.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Certificates.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Descriptor.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bazarish::client {

// This user's assigned serving destination + the public serving sealing key
// for it (from GET /v1/messaging/destination). Folded into the user-signed
// subscription certificate so contacts route and seal to it.
struct DestinationInfo {
    std::string dest;
    Bytes servingSealingKeyDer;
};

// Server reply to subscribe/renew: the granted lifecycle plus the serving
// destination + serving sealing key the certificate now carries.
struct SubscribeResult {
    std::int64_t notAfter = 0;
    std::uint64_t quotaBytes = 0;
    std::int64_t maxTermSeconds = 0;
    // The user's assigned serving destination and its serving sealing key.
    std::string dest;
    Bytes servingSealingKeyDer;
    // The subscription certificate we issued (DER): our self-signed contact
    // card (prekey + dest + serving sealing key), persisted so we can hand a
    // contact the full self-verifying card.
    Bytes subscriptionCertDer;
};

// A contact looked up by fingerprint: the user's subscription certificate,
// which carries the sealing prekey plus the routing (dest + serving sealing
// key). Verified before being returned.
struct ContactInfo {
    SubscriptionCertificate subscriptionCert;
};

struct Subscription {
    std::int64_t notAfter = 0;
    std::uint64_t quotaBytes = 0;
};

// A resolved alias: the self-verifying trust chain. The certificates are
// verified before being returned; user is the resolved fingerprint.
struct ResolveResult {
    std::string user;
    AliasCertificate aliasCert;
    SubscriptionCertificate subscriptionCert;
};

struct PendingEntry {
    std::string id;
    // Delivery class: "content" or "contact" (the server-visible admission
    // selector, not the end-to-end content type).
    std::string deliveryClass;
};

// Outcome of a send attempt. status is "pending", "delivered" or "failed";
// on failure errorCode carries the typed reason when recognized.
struct SendStatus {
    std::string status;
    std::optional<ErrorCode> errorCode;
    std::string errorMessage;
};

// Seals a delivery envelope to a destination server. deliveryClass is the
// server-visible admission selector ("content" or "contact"); mailbox is the
// recipient's fingerprint, messageId deduplicates retries, token is the
// one-time delivery token for "content" (absent for "contact"). The result is
// the opaque sealed blob the send endpoint expects.
Bytes sealDeliveryEnvelope(const std::string& deliveryClass, const std::string& mailbox,
    const std::string& messageId, const std::optional<Bytes>& token,
    const Key& recipientSealingKey);

// The client-side messenger: typed wrappers over the full client API,
// reusing the shared identity, certificate and crypto primitives. Owns the
// caller identity; non-copyable and non-movable (it hands out a reference to
// its identity internally).
class Client {
public:
    Client(Identity identity, std::string clientId, ServerEndpoint endpoint);

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    const Identity& identity() const;
    const std::string& clientId() const;
    const ServerEndpoint& endpoint() const;
    // The facade the transport is currently using (last that worked), as a URL.
    std::string activeFacadeUrl() const;

    // --- Account (service node) ---

    // sealingPrekeyDer, when non-empty, is published in the subscription
    // certificate so contacts can E2E-encrypt their first message.
    SubscribeResult subscribe(
        std::int64_t issuedAt, std::int64_t notAfter, const Bytes& sealingPrekeyDer = {});
    SubscribeResult renew(
        std::int64_t issuedAt, std::int64_t notAfter, const Bytes& sealingPrekeyDer = {});
    Subscription subscriptionStatus();
    void unsubscribe();
    // Hands the serving server a fresh offline transient (I2P-base64) so it can
    // operate the user's personal destination for the subscription window.
    // Requires an active i2pDest entitlement server-side; returns acceptance.
    bool sendI2pTransient(const std::string& transientB64, std::int64_t expiresUnix);
    void registerAlias(
        const std::string& alias, std::int64_t issuedAt, std::optional<std::int64_t> notAfter);
    void releaseAlias(const std::string& alias);
    ResolveResult resolve(const std::string& alias);
    // Looks up a user by fingerprint: their subscription certificate (sealing
    // prekey + routing). Verified.
    ContactInfo lookupContact(const std::string& peerFingerprint);
    // First-contact card fetch from a descriptor (fp + serving destination +
    // serving sealing key), via our own server's I2P proxy (POST
    // /v1/messaging/fetch op "card"). The query (which fingerprint) is sealed to
    // the serving server's key so our own server cannot read it; the response is
    // sealed to a fresh ephemeral key. Verifies the card and that it is for the
    // descriptor's fingerprint (see docs-main api/FederatedResolve.md).
    ContactInfo fetchCard(const Descriptor& descriptor);
    // This user's assigned serving destination + serving sealing key, from the
    // messaging server (GET /v1/messaging/destination).
    DestinationInfo myDestination();

    // --- Messaging (server) ---

    void registerThisClient();
    void retireClient(const std::string& clientId);
    void registerTokenHashes(const std::vector<Bytes>& hashes);
    void deleteTokenHashes(const std::vector<Bytes>& hashes);
    std::vector<PendingEntry> listPending();
    Bytes fetchBlob(const std::string& blobId);
    void ack(const std::string& blobId);
    std::string submitSend(
        const std::string& toDest, const Bytes& sealed, const Bytes& payload);
    SendStatus pollSend(const std::string& attemptId);

    // --- Large media: blob storage (rotating encrypted-LeaseSet, I2P) ---

    // Uploads a packed large blob's ciphertext to blob storage (PUT
    // /v1/storage/blob via the facade) with the given retention; returns the
    // capability fields for the sealed pointer.
    BlobUploadResult uploadBlob(const PackedBlob& packed, const BlobRetention& retention);
    // Streamed counterpart: uploads the ciphertext from a temp file without
    // holding it in memory (the large-file path).
    BlobUploadResult uploadBlobFromFile(
        const PackedBlobFile& packed, const BlobRetention& retention);

    // Fetches a blob through our own server's I2P proxy (the fallback when this
    // client has no local SAM bridge), verifying and decrypting it.
    Bytes fetchBlobViaProxy(const BlobPointer& pointer);

    // Deletes a blob (sender unsend) through our own server's I2P proxy.
    void deleteBlobViaProxy(const std::string& blobUrl, const std::string& deleteToken);

private:
    SubscribeResult submitSubscription(const std::string& path, std::int64_t issuedAt,
        std::int64_t notAfter, const Bytes& sealingPrekeyDer);

    const Identity identity_;
    ApiClient api_;
};

}  // namespace bazarish::client
